// Tests for platform/SafeFile against a scratch directory given on the
// command line. The directory must not exist yet; it is created and removed.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
  std::puts("safefile_tests: all passed");
  return EXIT_SUCCESS;
}
