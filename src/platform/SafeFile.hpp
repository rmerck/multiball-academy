#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pinball {

enum class ReadResult {
  Ok,
  Missing,     // Nothing at the path.
  NotRegular,  // A directory, device, pipe or symbolic link.
  TooLarge,    // More than the caller's limit.
  Error,       // Could not be opened or read.
};

// Reads a whole regular file of at most maxBytes into out. out is empty
// unless the result is Ok. A file that changes size while being read is
// refused. Never buffers more than one byte beyond the file's size.
//
// On POSIX systems the file is opened once and every check is made on that
// open handle, so what is checked is what is read. A symbolic link as the
// last part of the path is refused (links in directories above it are
// followed), and a pipe or device there cannot block the caller. Elsewhere
// plain SDL file calls are used: same limits, without those protections.
[[nodiscard]] ReadResult readRegularFile(const std::string& path, std::size_t maxBytes,
                                         std::vector<std::uint8_t>& out);

// Replaces the file at path with text, all or nothing. Returns false,
// leaving any existing file untouched, on failure.
//
// On POSIX systems the text goes to a new, uniquely named, owner-only file
// beside the target, is flushed to disk, and is renamed into place. No
// existing file or link is ever written through, and a link at path itself
// is replaced, not followed. The result is readable by its owner only.
// Elsewhere a fixed temporary name and plain SDL file calls are used.
[[nodiscard]] bool replaceFile(const std::string& path, std::string_view text);

// Removes temporary files that a replaceFile(path, ...) cut short by a crash
// left beside path. Only regular files that carry replaceFile's own temporary
// name and have not been written for minAgeSeconds are removed: a file of the
// user's with a similar name is not ours to delete, and a fresh temporary may
// be another running copy's save in progress. Returns how many were removed.
// File system errors are not reported; whatever could not be removed stays.
std::size_t removeStaleTemporaries(std::string_view path, long long minAgeSeconds = 60);

}  // namespace pinball
