// Tests for platform/SafeFile against a scratch directory given on the
// command line. The directory must not exist yet; it is created and removed.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "platform/SafeFile.hpp"

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#include <unistd.h>
#define PINBALL_POSIX_TESTS 1
#endif

// True where renameNoReplace has only link() and unlink() to work with: on a
// POSIX system without the single call, or when a build leaves that call out
// on purpose (the safe_files_fallback test), so that the path other systems
// depend on is run here too.
#if defined(PINBALL_POSIX_TESTS) && \
    (defined(PINBALL_NO_ATOMIC_NOREPLACE) || !(defined(__linux__) || defined(__APPLE__)))
#define PINBALL_LINK_FALLBACK 1
#endif

#if defined(__linux__)
#include <sys/syscall.h>

#include <cerrno>
#define PINBALL_FSYNC_TESTS 1

namespace {
// When not zero, the next flushes fail with this error.
int forcedFsyncError = 0;
}  // namespace

// Stands in for the C library's fsync in this program, so that a file system
// that refuses or fails a flush can be imitated. (Linux only: on macOS the
// save asks for a full flush through fcntl first.)
extern "C" int fsync(int fd) {
  if (forcedFsyncError != 0) {
    errno = forcedFsyncError;
    return -1;
  }
  return static_cast<int>(::syscall(SYS_fsync, fd));
}
#endif

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(expr) check((expr), #expr, __LINE__)

std::string slurp(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

void spit(const std::filesystem::path& path, std::string_view text) {
  std::ofstream out(path, std::ios::binary);
  out << text;
}

}  // namespace

int main(int argc, char* argv[]) {
  namespace fs = std::filesystem;
  using pinball::ReadResult;
  if (argc != 2) {
    std::fputs("Usage: safefile_tests <new scratch directory>\n", stderr);
    return EXIT_FAILURE;
  }
  const fs::path dir = argv[1];
  std::error_code error;
  if (fs::exists(dir, error) || !fs::create_directories(dir, error)) {
    std::fputs("scratch directory must be new\n", stderr);
    return EXIT_FAILURE;
  }

  std::vector<std::uint8_t> bytes;
  const fs::path file = dir / "data.txt";

  // Reading.
  CHECK(pinball::readRegularFile(file.string(), 16, bytes) == ReadResult::Missing);
  spit(file, "hello");
  CHECK(pinball::readRegularFile(file.string(), 16, bytes) == ReadResult::Ok);
  CHECK(std::string(bytes.begin(), bytes.end()) == "hello");
  CHECK(pinball::readRegularFile(file.string(), 5, bytes) == ReadResult::Ok && bytes.size() == 5);
  CHECK(pinball::readRegularFile(file.string(), 4, bytes) == ReadResult::TooLarge);
  CHECK(bytes.empty());
  spit(dir / "empty", "");
  CHECK(pinball::readRegularFile((dir / "empty").string(), 4, bytes) == ReadResult::Ok);
  CHECK(bytes.empty());
  CHECK(pinball::readRegularFile(dir.string(), 16, bytes) != ReadResult::Ok);
  CHECK(bytes.empty());
  // Limits at the extremes.
  CHECK(pinball::readRegularFile((dir / "empty").string(), 0, bytes) == ReadResult::Ok);
  CHECK(pinball::readRegularFile(file.string(), 0, bytes) == ReadResult::TooLarge);
  CHECK(pinball::readRegularFile(file.string(), SIZE_MAX, bytes) == ReadResult::Error);
  CHECK(bytes.empty());
  // A generous limit does not mean a generous buffer.
  CHECK(pinball::readRegularFile(file.string(), 1uz << 30U, bytes) == ReadResult::Ok);
  CHECK(bytes.size() == 5 && bytes.capacity() < 4096);

  // Writing.
  const fs::path target = dir / "scores.txt";
  CHECK(pinball::replaceFile(target.string(), "one\n"));
  CHECK(slurp(target) == "one\n");
  CHECK(pinball::replaceFile(target.string(), "two\n"));
  CHECK(slurp(target) == "two\n");
  CHECK(pinball::replaceFile(target.string(), ""));
  CHECK(slurp(target).empty());
  // A missing directory fails and leaves nothing behind.
  CHECK(!pinball::replaceFile((dir / "absent" / "x.txt").string(), "x"));

#if defined(PINBALL_POSIX_TESTS)
  // A symbolic link is refused even when it points at a good file.
  const fs::path link = dir / "link.txt";
  fs::create_symlink(file, link);
  CHECK(pinball::readRegularFile(link.string(), 16, bytes) == ReadResult::NotRegular);
  CHECK(bytes.empty());

  // A pipe is refused at once; it does not block waiting for a writer.
  const fs::path pipe = dir / "pipe";
  CHECK(::mkfifo(pipe.c_str(), 0600) == 0);
  CHECK(pinball::readRegularFile(pipe.string(), 16, bytes) == ReadResult::NotRegular);

  CHECK(pinball::readRegularFile(dir.string(), 16, bytes) == ReadResult::NotRegular);
  // A dangling link is refused too, not reported as missing.
  fs::create_symlink(dir / "nowhere", dir / "dangling");
  CHECK(pinball::readRegularFile((dir / "dangling").string(), 16, bytes) ==
        ReadResult::NotRegular);

  // The saved file gives nothing to anyone but its owner.
  struct stat info {};
  CHECK(::stat(target.c_str(), &info) == 0 && (info.st_mode & 0077) == 0);

  // Saving leaves no temporary files behind: only the files made above.
  const fs::path victim = dir / "victim.txt";
  spit(victim, "untouched");
  CHECK(pinball::replaceFile(target.string(), "three\n"));
  CHECK(slurp(target) == "three\n");
  std::size_t entries = 0;
  for (const auto& entry : fs::directory_iterator(dir)) {
    (void)entry;
    ++entries;
  }
  CHECK(entries == 7);  // data, empty, scores, link, pipe, dangling, victim.

  // A link at the destination is replaced by a real file; its target is kept.
  fs::remove(target);
  fs::create_symlink(victim, target);
  CHECK(pinball::replaceFile(target.string(), "four\n"));
  CHECK(!fs::is_symlink(target) && slurp(target) == "four\n");
  CHECK(slurp(victim) == "untouched");
#endif

#if defined(PINBALL_POSIX_TESTS)
  // Permissions of the result: owner-only unless asked, under any umask;
  // Shared is 0644 less the umask, and never less than the owner's own.
  {
    using pinball::FileAccess;
    const fs::path sub = dir / "modes";
    fs::create_directory(sub, error);
    const std::string secret = (sub / "scores.txt").string();
    const std::string picture = (sub / "frame.png").string();
    const auto modeOf = [](const std::string& path) {
      struct stat status {};
      return ::stat(path.c_str(), &status) == 0 ? static_cast<unsigned>(status.st_mode & 07777U)
                                                : 07777U + 1U;
    };
    const mode_t original = ::umask(022);
    for (const unsigned mask : {022U, 002U, 027U, 077U, 0U, 0377U, 0777U}) {
      ::umask(static_cast<mode_t>(mask));
      const bool savedSecret = pinball::replaceFile(secret, "s");
      const bool savedDefault = pinball::replaceFile(secret, "secret", FileAccess::OwnerOnly);
      const bool savedPicture = pinball::replaceFile(picture, "picture", FileAccess::Shared);
      // The call leaves the umask as it found it.
      const auto after = static_cast<unsigned>(::umask(022));
      CHECK(savedSecret && savedDefault && savedPicture);
      CHECK(after == mask);
      CHECK(modeOf(secret) == 0600U);
      CHECK(modeOf(picture) == (0644U & ~(mask & 0077U)));
      CHECK(slurp(secret) == "secret" && slurp(picture) == "picture");
    }
    // The mode asked for wins over the mode of the file replaced, both ways.
    ::umask(022);
    CHECK(pinball::replaceFile(secret, "open", FileAccess::Shared) && modeOf(secret) == 0644U);
    CHECK(pinball::replaceFile(picture, "shut") && modeOf(picture) == 0600U);
    CHECK(::chmod(picture.c_str(), 0666) == 0);
    CHECK(pinball::replaceFile(picture, "shut again") && modeOf(picture) == 0600U);
    ::umask(original);
  }
#endif

#if defined(PINBALL_FSYNC_TESTS)
  // A file system with no flush to offer does not stop a save; a flush that
  // failed does, and leaves the old file and no temporary behind.
  {
    const fs::path sub = dir / "flush";
    fs::create_directory(sub, error);
    const fs::path saved = sub / "scores.txt";
    for (const int unsupported : {EINVAL, ENOTSUP, EOPNOTSUPP}) {
      forcedFsyncError = unsupported;
      CHECK(pinball::replaceFile(saved.string(), "kept\n"));
      CHECK(slurp(saved) == "kept\n");
    }
    for (const int failed : {EIO, ENOSPC, EDQUOT, EROFS, EBADF}) {
      forcedFsyncError = failed;
      CHECK(!pinball::replaceFile(saved.string(), "lost\n"));
      CHECK(slurp(saved) == "kept\n");
    }
    forcedFsyncError = 0;
    CHECK(pinball::replaceFile(saved.string(), "again\n"));
    CHECK(slurp(saved) == "again\n");
    std::size_t left = 0;
    for (const auto& entry : fs::directory_iterator(sub)) {
      (void)entry;
      ++left;
    }
    CHECK(left == 1);
  }
#endif

  // Moving without replacing, and setting a file aside under a free name.
  {
    using pinball::MoveResult;
    const fs::path sub = dir / "aside";
    fs::create_directory(sub, error);
    const std::string from = (sub / "from.txt").string();
    const std::string to = (sub / "to.txt").string();
    spit(from, "moved");
    CHECK(pinball::renameNoReplace(from, to) == MoveResult::Moved);
    CHECK(!fs::exists(from) && slurp(to) == "moved");
    // A taken name is refused, and both files are as they were.
    spit(from, "second");
    CHECK(pinball::renameNoReplace(from, to) == MoveResult::Exists);
    CHECK(slurp(from) == "second" && slurp(to) == "moved");
    // Nothing to move.
    CHECK(pinball::renameNoReplace((sub / "absent").string(), (sub / "new").string()) ==
          MoveResult::Error);
    CHECK(!fs::exists(sub / "new"));
    // A directory. The single system call moves it; the link() fallback
    // cannot, and must say so with the directory and its contents in place
    // and nothing made at the new name. A taken name is refused either way.
    {
      const fs::path folder = sub / "folder";
      const fs::path moved = sub / "folder2";
      fs::create_directory(folder, error);
      spit(folder / "inside.txt", "inside");
      CHECK(pinball::renameNoReplace(folder.string(), to) == MoveResult::Exists);
      CHECK(slurp(folder / "inside.txt") == "inside" && slurp(to) == "moved");
#if defined(PINBALL_LINK_FALLBACK)
      CHECK(pinball::renameNoReplace(folder.string(), moved.string()) == MoveResult::Error);
      CHECK(slurp(folder / "inside.txt") == "inside" && !fs::exists(moved));
      CHECK(pinball::setAside(folder.string()).empty());
      CHECK(fs::is_directory(folder) && !fs::exists(folder.string() + ".bad"));
#else
      CHECK(pinball::renameNoReplace(folder.string(), moved.string()) == MoveResult::Moved);
      CHECK(!fs::exists(folder) && slurp(moved / "inside.txt") == "inside");
      CHECK(pinball::setAside(moved.string()) == moved.string() + ".bad");
      CHECK(slurp(fs::path(moved.string() + ".bad") / "inside.txt") == "inside");
#endif
    }
#if defined(PINBALL_POSIX_TESTS)
    // A link that points nowhere still holds its name: not replaced, and
    // nothing appears where it points.
    const fs::path hole = sub / "hole";
    fs::create_symlink(hole, sub / "dangling");
    CHECK(pinball::renameNoReplace(from, (sub / "dangling").string()) == MoveResult::Exists);
    CHECK(slurp(from) == "second" && fs::is_symlink(sub / "dangling") && !fs::exists(hole));
    // A link is moved itself; what it points at stays.
    fs::create_symlink(to, sub / "link");
    CHECK(pinball::renameNoReplace((sub / "link").string(), (sub / "link2").string()) ==
          MoveResult::Moved);
    CHECK(fs::is_symlink(sub / "link2") && !fs::is_symlink(to) && slurp(to) == "moved");
#endif

    // Setting aside: an earlier .bad is kept, and each later one gets the
    // next number.
    const std::string scores = (sub / "scores.txt").string();
    spit(scores, "newer format");
    CHECK(pinball::setAside(scores) == scores + ".bad");
    spit(scores, "garbage");
    CHECK(pinball::setAside(scores) == scores + ".bad.1");
    CHECK(slurp(scores + ".bad") == "newer format" && slurp(scores + ".bad.1") == "garbage");
    CHECK(!fs::exists(scores));
    // A gap is filled first.
    spit(scores + ".bad.3", "three");
    spit(scores, "two");
    CHECK(pinball::setAside(scores) == scores + ".bad.2");
    // With every name taken the file stays and nothing set aside changes.
    for (int number = 4; number <= 9; ++number) {
      spit(scores + ".bad." + std::to_string(number), "old");
    }
    spit(scores, "eleventh");
    CHECK(pinball::setAside(scores).empty());
    CHECK(slurp(scores) == "eleventh");
    CHECK(slurp(scores + ".bad") == "newer format" && slurp(scores + ".bad.1") == "garbage");
    CHECK(slurp(scores + ".bad.2") == "two" && slurp(scores + ".bad.3") == "three");
    CHECK(slurp(scores + ".bad.9") == "old" && !fs::exists(scores + ".bad.10"));
    // A smaller cap, and another suffix.
    CHECK(pinball::setAside(scores, ".old", 0) == scores + ".old");
    spit(scores, "twelfth");
    CHECK(pinball::setAside(scores, ".old", 0).empty() && slurp(scores) == "twelfth");
    // Nothing there to set aside.
    CHECK(pinball::setAside((sub / "absent.txt").string()).empty());
    CHECK(!fs::exists(sub / "absent.txt.bad"));
#if defined(PINBALL_POSIX_TESTS)
    // A link in the score file's place is set aside as a link.
    fs::remove(scores);
    fs::create_symlink(to, scores);
    CHECK(pinball::setAside(scores, ".link") == scores + ".link");
    CHECK(fs::is_symlink(scores + ".link") && slurp(to) == "moved");
#endif
  }

  // Sweeping up after an interrupted save removes only old temporaries of
  // that one target. (Suffixes here are the six characters mkstemp adds.)
  {
    const auto old = fs::file_time_type::clock::now() - std::chrono::minutes(5);
    const auto age = [&](const fs::path& path) { fs::last_write_time(path, old, error); };
    const std::string base = target.string();
    const fs::path stale = base + ".tmp-Ab3dE9";
    const fs::path fresh = base + ".tmp-Zz0000";
    const fs::path backup = base + ".backup";       // A user's file: six characters too.
    const fs::path oldStyle = base + ".Ab3dE9";     // Not the temporary name.
    const fs::path shortName = base + ".tmp-Ab3dE";
    const fs::path longName = base + ".tmp-Ab3dE9x";
    const fs::path oddName = base + ".tmp-Ab3d.9";
    const fs::path other = dir / "other.txt.tmp-Ab3dE9";  // Another target's.
    const fs::path folder = base + ".tmp-Folder";
    // Written "after now", as when the system clock has since been set back.
    const fs::path future = base + ".tmp-Future";
    for (const fs::path& path :
         {stale, fresh, backup, oldStyle, shortName, longName, oddName, other, future}) {
      spit(path, "x");
    }
    fs::last_write_time(future, fs::file_time_type::clock::now() + std::chrono::minutes(5), error);
    fs::create_directory(folder, error);
    for (const fs::path& path : {stale, backup, oldStyle, shortName, longName, oddName, other,
                                 folder}) {
      age(path);
    }
#if defined(PINBALL_POSIX_TESTS)
    // A link wearing the temporary name is left alone, and so is its target.
    const fs::path linked = base + ".tmp-L1nk3d";
    fs::create_symlink(stale, linked);
#endif
    CHECK(pinball::removeStaleTemporaries(base) == 1);
    CHECK(!fs::exists(stale));
    for (const fs::path& path :
         {fresh, backup, oldStyle, shortName, longName, oddName, other, folder, future}) {
      CHECK(fs::exists(path));
    }
#if defined(PINBALL_POSIX_TESTS)
    CHECK(fs::is_symlink(linked));
#endif
    CHECK(fs::exists(target));  // The target itself is kept.
    // Nothing left to do a second time; with no age limit the fresh one goes.
    CHECK(pinball::removeStaleTemporaries(base) == 0);
    CHECK(pinball::removeStaleTemporaries(base, 0) == 1);
    CHECK(!fs::exists(fresh) && fs::exists(backup));
    // Its age is negative, so no limit makes it old.
    CHECK(fs::exists(future));
    // A directory that is not there is not an error.
    CHECK(pinball::removeStaleTemporaries((dir / "absent" / "x.txt").string()) == 0);
  }

  fs::remove_all(dir, error);
  if (failures != 0) {
    std::fprintf(stderr, "safefile_tests: %d failure(s)\n", failures);
    return EXIT_FAILURE;
  }
#if defined(PINBALL_NO_ATOMIC_NOREPLACE)
  std::puts("safefile_tests (link fallback): all passed");
#else
  std::puts("safefile_tests: all passed");
#endif
  return EXIT_SUCCESS;
}
