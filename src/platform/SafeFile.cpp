#include "platform/SafeFile.hpp"

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
  int get() const noexcept { return fd_; }
  bool ok() const noexcept { return fd_ >= 0; }
  // Closes now and reports whether that succeeded.
  bool close() noexcept {
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
  const Descriptor file(
      ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY | O_CLOEXEC));
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
  if (total > expected) {
    // It grew under us: what was read is not a whole, settled file.
    out.clear();
    return total > maxBytes ? ReadResult::TooLarge : ReadResult::Error;
  }
  out.resize(total);
  return ReadResult::Ok;
}

bool replaceFile(const std::string& path, std::string_view text) {
  // A fresh, uniquely named file beside the target, created exclusively and
  // for its owner only. Nothing already at any name is opened, and two
  // programs saving at once cannot disturb each other's file.
  std::string temporary = path + ".XXXXXX";
  Descriptor file(::mkostemp(temporary.data(), O_CLOEXEC));
  if (!file.ok()) {
    return false;
  }
  // The data must be on disk before the rename makes it the real file.
  bool ok = writeAll(file.get(), text);
  if (ok) {
    int synced = -1;
    do {
      synced = ::fsync(file.get());
    } while (synced != 0 && errno == EINTR);
    ok = synced == 0;
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
  if (failed || total > expected) {
    out.clear();
    return total > maxBytes ? ReadResult::TooLarge : ReadResult::Error;
  }
  out.resize(total);
  return ReadResult::Ok;
}

bool replaceFile(const std::string& path, std::string_view text) {
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

#endif

}  // namespace pinball
