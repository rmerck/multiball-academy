#include "platform/SafeFile.hpp"

#if defined(__unix__) || defined(__APPLE__)
#define PINBALL_POSIX_FILES 1
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#else
#include <SDL3/SDL.h>
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
  // No link following on the last component. Non-blocking, so opening a
  // pipe put at the path returns at once; it is then refused below.
  const Descriptor file(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  if (!file.ok()) {
    if (errno == ENOENT) {
      return ReadResult::Missing;
    }
    return errno == ELOOP ? ReadResult::NotRegular : ReadResult::Error;
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

  // One byte more than allowed, to notice a file that grows while read.
  out.resize(maxBytes + 1U);
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
  if (total > maxBytes) {
    out.clear();
    return ReadResult::TooLarge;
  }
  out.resize(total);
  return ReadResult::Ok;
}

bool replaceFile(const std::string& path, std::string_view text) {
  const std::string temporary = path + ".tmp";
  // Clear any leftover, then create the file anew: O_EXCL fails if anything,
  // a link included, appears at the name in between. Owner access only.
  ::unlink(temporary.c_str());
  Descriptor file(::open(temporary.c_str(),
                         O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, S_IRUSR | S_IWUSR));
  if (!file.ok()) {
    return false;
  }
  // The data must be on disk before the rename makes it the real file.
  bool ok = writeAll(file.get(), text) && ::fsync(file.get()) == 0;
  ok = file.close() && ok;
  ok = ok && std::rename(temporary.c_str(), path.c_str()) == 0;
  if (!ok) {
    ::unlink(temporary.c_str());
  }
  return ok;
}

#else  // Other platforms: the portable SDL calls, with the same checks.

ReadResult readRegularFile(const std::string& path, std::size_t maxBytes,
                           std::vector<std::uint8_t>& out) {
  out.clear();
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
  out.resize(maxBytes + 1U);
  std::size_t total = 0;
  while (total < out.size()) {
    const std::size_t got = SDL_ReadIO(stream, out.data() + total, out.size() - total);
    if (got == 0) {
      break;
    }
    total += got;
  }
  SDL_CloseIO(stream);
  if (total > maxBytes) {
    out.clear();
    return ReadResult::TooLarge;
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
