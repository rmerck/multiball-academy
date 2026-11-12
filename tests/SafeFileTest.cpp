// Tests for platform/SafeFile against a scratch directory given on the
// command line. The directory must not exist yet; it is created and removed.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
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
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void spit(const std::filesystem::path& path, const std::string& text) {
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

  // Writing.
  const fs::path target = dir / "scores.txt";
  CHECK(pinball::replaceFile(target.string(), "one\n"));
  CHECK(slurp(target) == "one\n");
  CHECK(pinball::replaceFile(target.string(), "two\n"));
  CHECK(slurp(target) == "two\n");
  CHECK(!fs::exists(dir / "scores.txt.tmp"));
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

  // The saved file is private to its owner.
  struct stat info {};
  CHECK(::stat(target.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600);

  // A link planted at the temporary name is replaced, never written through.
  const fs::path victim = dir / "victim.txt";
  spit(victim, "untouched");
  fs::create_symlink(victim, dir / "scores.txt.tmp");
  CHECK(pinball::replaceFile(target.string(), "three\n"));
  CHECK(slurp(target) == "three\n");
  CHECK(slurp(victim) == "untouched");
  CHECK(!fs::is_symlink(target));

  // A link at the destination is replaced by a real file; its target is kept.
  fs::remove(target);
  fs::create_symlink(victim, target);
  CHECK(pinball::replaceFile(target.string(), "four\n"));
  CHECK(!fs::is_symlink(target) && slurp(target) == "four\n");
  CHECK(slurp(victim) == "untouched");
#endif

  fs::remove_all(dir, error);
  if (failures != 0) {
    std::fprintf(stderr, "safefile_tests: %d failure(s)\n", failures);
    return EXIT_FAILURE;
  }
  std::puts("safefile_tests: all passed");
  return EXIT_SUCCESS;
}
