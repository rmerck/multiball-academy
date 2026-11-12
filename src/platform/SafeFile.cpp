#include "platform/SafeFile.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#define PINBALL_POSIX_FILES 1
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <limits>
#else
#include <SDL3/SDL.h>

#include <limits>
#endif

namespace pinball {

namespace {

// What replaceFile puts between the target's name and the six characters
// that make its temporary file unique. removeStaleTemporaries looks for the
// same mark, so the two cannot drift apart.
constexpr std::string_view kTemporaryMark = ".tmp-";
constexpr std::size_t kTemporarySuffixLength = 6;

// True if name is prefix followed by exactly the unique suffix. The suffix
// is letters and digits on every system's mkstemp.
bool isTemporaryName(std::string_view name, std::string_view prefix) noexcept {
  if (name.size() != prefix.size() + kTemporarySuffixLength || !name.starts_with(prefix)) {
    return false;
  }
  return std::ranges::all_of(name.substr(prefix.size()), [](char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
  });
}

}  // namespace

std::size_t removeStaleTemporaries(std::string_view path, long long minAgeSeconds) {
  namespace fs = std::filesystem;
  const fs::path target(path);
  const std::string prefix = target.filename().string() + std::string(kTemporaryMark);
  const fs::path directory = target.has_parent_path() ? target.parent_path() : fs::path(".");
  const auto now = fs::file_time_type::clock::now();
  std::size_t removed = 0;
  std::error_code error;
  for (fs::directory_iterator it(directory, error), end; !error && it != end;
       it.increment(error)) {
    if (!isTemporaryName(it->path().filename().string(), prefix)) {
      continue;
    }
    // A problem with one entry must not end the sweep, so it has its own
    // error. Links and directories are never touched.
    std::error_code entryError;
    if (fs::symlink_status(it->path(), entryError).type() != fs::file_type::regular) {
      continue;
    }
    const fs::file_time_type written = fs::last_write_time(it->path(), entryError);
    if (entryError || now - written < std::chrono::seconds(minAgeSeconds)) {
      continue;
    }
    if (fs::remove(it->path(), entryError)) {
      ++removed;
    }
  }
  return removed;
}

std::string setAside(const std::string& path, std::string_view suffix, unsigned maxNumbered) {
  for (unsigned number = 0; number <= maxNumbered; ++number) {
    std::string name = path + std::string(suffix);
    if (number > 0) {
      name += "." + std::to_string(number);
    }
    switch (renameNoReplace(path, name)) {
      case MoveResult::Moved:
        return name;
      case MoveResult::Exists:
        break;  // Taken: try the next name.
      case MoveResult::Error:
        return {};
    }
  }
  return {};
}

#if defined(PINBALL_POSIX_FILES)

namespace {

// Closes a descriptor on every path out of a function.
class Descriptor {
 public:
  explicit Descriptor(int fd) noexcept : fd_(fd) {}
  ~Descriptor() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] bool ok() const noexcept { return fd_ >= 0; }
  // Closes now and reports whether that succeeded.
  [[nodiscard]] bool close() noexcept {
    const int fd = fd_;
    fd_ = -1;
    return fd >= 0 && ::close(fd) == 0;
  }

 private:
  int fd_;
};

bool writeAll(int fd, std::string_view text) noexcept {
  std::size_t done = 0;
  while (done < text.size()) {
    const ssize_t wrote = ::write(fd, text.data() + done, text.size() - done);
    if (wrote < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (wrote == 0) {
      return false;
    }
    done += static_cast<std::size_t>(wrote);
  }
  return true;
}

// True if a failed flush means "this file system has no flush", not "the
// data did not reach the disk". Home directories on some network and FUSE
// mounts answer this way; the write itself has already succeeded there.
// Real failures (EIO, ENOSPC, EDQUOT, EROFS and the rest) are not in this
// list.
constexpr bool flushIsUnsupported(int error) noexcept {
#if defined(EOPNOTSUPP) && EOPNOTSUPP != ENOTSUP
  if (error == EOPNOTSUPP) {
    return true;
  }
#endif
  return error == EINVAL || error == ENOTSUP;
}

// The permission bits for a file made with the given access. mkstemp ignores
// the umask, so Shared applies it here. There is no call that only reads the
// umask: it is set to owner-only and put straight back, so if another
// thread makes a file in that instant, the file comes out stricter, never
// looser.
mode_t modeFor(FileAccess access) noexcept {
  if (access == FileAccess::OwnerOnly) {
    return S_IRUSR | S_IWUSR;
  }
  const mode_t mask = ::umask(S_IRWXG | S_IRWXO);
  ::umask(mask);
  const mode_t shared = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
  // The owner's own bits are kept: the file is still ours to replace.
  return static_cast<mode_t>(shared & ~(mask & (S_IRWXG | S_IRWXO)));
}

// Pushes a file's data to the disk itself, not only to the drive's cache.
// Succeeds, having done what can be done, where there is no flush to ask for.
bool syncToDisk(int fd) noexcept {
#if defined(__APPLE__) && defined(F_FULLFSYNC)
  // On macOS plain fsync stops at the drive. Not every volume supports the
  // full flush, so fall through to fsync if it is refused.
  if (::fcntl(fd, F_FULLFSYNC) == 0) {
    return true;
  }
#endif
  int synced = -1;
  do {
    synced = ::fsync(fd);
  } while (synced != 0 && errno == EINTR);
  return synced == 0 || flushIsUnsupported(errno);
}

}  // namespace

ReadResult readRegularFile(const std::string& path, std::size_t maxBytes,
                           std::vector<std::uint8_t>& out) {
  out.clear();
  if (maxBytes == std::numeric_limits<std::size_t>::max()) {
    return ReadResult::Error;  // No room for the one byte beyond the limit.
  }
  // No link following on the last component. Non-blocking, so opening a
  // pipe put at the path returns at once; it is then refused below. Never
  // adopts a terminal found there.
  int opened = -1;
  do {
    opened = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
  } while (opened < 0 && errno == EINTR);
  const Descriptor file(opened);
  if (!file.ok()) {
    switch (errno) {
      case ENOENT:
        return ReadResult::Missing;
      case ELOOP:   // A link, on Linux and macOS.
      case EMLINK:  // A link, on FreeBSD.
#if defined(EFTYPE)
      case EFTYPE:  // A link, on NetBSD.
#endif
      case ENXIO:   // A socket, or a device with nothing behind it.
#if defined(EOPNOTSUPP)
      case EOPNOTSUPP:  // A socket, on macOS and FreeBSD.
#endif
        return ReadResult::NotRegular;
      default:
        return ReadResult::Error;
    }
  }
  struct stat info {};
  if (::fstat(file.get(), &info) != 0) {
    return ReadResult::Error;
  }
  if (!S_ISREG(info.st_mode)) {
    return ReadResult::NotRegular;
  }
  if (info.st_size < 0 || static_cast<std::uint64_t>(info.st_size) > maxBytes) {
    return ReadResult::TooLarge;
  }

  // One byte more than the file held when opened, to notice it growing
  // while read. Sized by the file, not by the caller's limit.
  const auto expected = static_cast<std::size_t>(info.st_size);
  out.resize(expected + 1U);
  std::size_t total = 0;
  while (total < out.size()) {
    const ssize_t got = ::read(file.get(), out.data() + total, out.size() - total);
    if (got < 0) {
      if (errno == EINTR) {
        continue;
      }
      out.clear();
      return ReadResult::Error;
    }
    if (got == 0) {
      break;
    }
    total += static_cast<std::size_t>(got);
  }
  if (total != expected) {
    // It grew or shrank under us: what was read is not a whole, settled file.
    out.clear();
    return total > maxBytes ? ReadResult::TooLarge : ReadResult::Error;
  }
  out.resize(total);
  return ReadResult::Ok;
}

bool replaceFile(const std::string& path, std::string_view text, FileAccess access) {
  // A fresh, uniquely named file beside the target, created exclusively and
  // for its owner only. Nothing already at any name is opened, and two
  // programs saving at once cannot disturb each other's file.
  std::string temporary = path + std::string(kTemporaryMark) + "XXXXXX";
  Descriptor file(::mkostemp(temporary.data(), O_CLOEXEC));
  if (!file.ok()) {
    return false;
  }
  // The final permissions go on while the file still has its temporary
  // name. mkstemp has made it 0600 (less under an odd umask), so for
  // OwnerOnly there is normally nothing to change. A refusal does not stop
  // the save: a file system that keeps no permissions (FAT, say) refuses,
  // and shows the same ones whatever is asked.
  const mode_t wanted = modeFor(access);
  struct stat made {};
  if (::fstat(file.get(), &made) != 0 || (made.st_mode & 07777) != wanted) {
    (void)::fchmod(file.get(), wanted);
  }
  // The data must be on disk before the rename makes it the real file.
  bool ok = writeAll(file.get(), text);
  if (ok) {
    ok = syncToDisk(file.get());
  }
  // Once the data is safely down, an interrupted close has still closed.
  const bool closed = file.close() || errno == EINTR;
  ok = ok && closed && std::rename(temporary.c_str(), path.c_str()) == 0;
  if (!ok) {
    ::unlink(temporary.c_str());
    return false;
  }
  // Best effort: make the rename itself survive a power cut.
  const std::size_t slash = path.find_last_of('/');
  const std::string directory = slash == std::string::npos ? "." : path.substr(0, slash + 1);
  const Descriptor parent(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  if (parent.ok()) {
    ::fsync(parent.get());
  }
  return true;
}

MoveResult renameNoReplace(const std::string& from, const std::string& to) {
  // PINBALL_NO_ATOMIC_NOREPLACE exists to build the fallback on a system
  // that has the single call, so that it can be tested there.
#if !defined(PINBALL_NO_ATOMIC_NOREPLACE)
#if defined(__linux__) && defined(RENAME_NOREPLACE)
  if (::renameat2(AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE) == 0) {
    return MoveResult::Moved;
  }
  if (errno == EEXIST) {
    return MoveResult::Exists;
  }
  // An old kernel, or a file system without the flag: use the fallback.
  if (errno != ENOSYS && errno != EINVAL && errno != ENOTSUP) {
    return MoveResult::Error;
  }
#elif defined(__APPLE__) && defined(RENAME_EXCL)
  if (::renamex_np(from.c_str(), to.c_str(), RENAME_EXCL) == 0) {
    return MoveResult::Moved;
  }
  if (errno == EEXIST) {
    return MoveResult::Exists;
  }
  if (errno != ENOTSUP && errno != EINVAL) {
    return MoveResult::Error;
  }
#endif
#endif
  // A second name for the same file, which the system refuses to make if
  // the name is taken, then the first name is dropped. Without
  // AT_SYMLINK_FOLLOW a link at from is given the new name itself.
  if (::linkat(AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), 0) != 0) {
    if (errno == EEXIST) {
      return MoveResult::Exists;
    }
    // Systems differ in which error they report first when the source cannot
    // be linked (a directory) and the name is also taken. A taken name is the
    // answer the caller needs, so look for it; nothing has been changed.
    struct stat taken {};
    return ::lstat(to.c_str(), &taken) == 0 ? MoveResult::Exists : MoveResult::Error;
  }
  if (::unlink(from.c_str()) != 0) {
    // Still there under both names: take the new one away again.
    ::unlink(to.c_str());
    return MoveResult::Error;
  }
  return MoveResult::Moved;
}

#else  // Other platforms: the portable SDL calls, with the same checks.

ReadResult readRegularFile(const std::string& path, std::size_t maxBytes,
                           std::vector<std::uint8_t>& out) {
  out.clear();
  if (maxBytes == std::numeric_limits<std::size_t>::max()) {
    return ReadResult::Error;
  }
  SDL_PathInfo info{};
  if (!SDL_GetPathInfo(path.c_str(), &info)) {
    return ReadResult::Missing;
  }
  if (info.type != SDL_PATHTYPE_FILE) {
    return ReadResult::NotRegular;
  }
  if (info.size > maxBytes) {
    return ReadResult::TooLarge;
  }
  SDL_IOStream* const stream = SDL_IOFromFile(path.c_str(), "rb");
  if (stream == nullptr) {
    return ReadResult::Error;
  }
  const auto expected = static_cast<std::size_t>(info.size);
  out.resize(expected + 1U);
  std::size_t total = 0;
  bool failed = false;
  while (total < out.size()) {
    const std::size_t got = SDL_ReadIO(stream, out.data() + total, out.size() - total);
    if (got == 0) {
      failed = SDL_GetIOStatus(stream) != SDL_IO_STATUS_EOF;
      break;
    }
    total += got;
  }
  SDL_CloseIO(stream);
  if (failed || total != expected) {
    out.clear();
    return total > maxBytes ? ReadResult::TooLarge : ReadResult::Error;
  }
  out.resize(total);
  return ReadResult::Ok;
}

bool replaceFile(const std::string& path, std::string_view text, FileAccess /*access*/) {
  const std::string temporary = path + ".tmp";
  SDL_RemovePath(temporary.c_str());
  SDL_IOStream* const stream = SDL_IOFromFile(temporary.c_str(), "wb");
  if (stream == nullptr) {
    return false;
  }
  bool ok = SDL_WriteIO(stream, text.data(), text.size()) == text.size();
  ok = SDL_FlushIO(stream) && ok;
  ok = SDL_CloseIO(stream) && ok;
  ok = ok && SDL_RenamePath(temporary.c_str(), path.c_str());
  if (!ok) {
    SDL_RemovePath(temporary.c_str());
  }
  return ok;
}

MoveResult renameNoReplace(const std::string& from, const std::string& to) {
  // Checked, then renamed: two steps, as the header says.
  if (SDL_GetPathInfo(to.c_str(), nullptr)) {
    return MoveResult::Exists;
  }
  return SDL_RenamePath(from.c_str(), to.c_str()) ? MoveResult::Moved : MoveResult::Error;
}

#endif

}  // namespace pinball
