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
// A file system that has no flush to offer (some network and FUSE mounts)
// does not fail the save; a flush that was tried and failed does.
// Elsewhere a fixed temporary name and plain SDL file calls are used.
[[nodiscard]] bool replaceFile(const std::string& path, std::string_view text);

enum class MoveResult {
  Moved,
  Exists,  // Something already has the new name; nothing was changed.
  Error,   // Could not be moved; nothing was changed.
};

// Gives the file, link or directory at from the name to, in the same
// directory or file system, only if nothing has that name. Whatever is at to,
// even a link that points nowhere, is never replaced or followed, and a link
// at from is moved itself, not what it points at.
//
// On Linux and macOS the system makes the check and the move one step
// (renameat2 with RENAME_NOREPLACE, renamex_np with RENAME_EXCL). Where the
// file system or an older system refuses that call, and on other POSIX
// systems, a second name is made with link(), which fails if the name is
// taken, and the first is then removed: still never an overwrite, but a
// directory cannot be moved that way and is an Error. Elsewhere the name is
// checked and then renamed with plain SDL calls, which leaves a moment
// between the two in which another program could take the name.
[[nodiscard]] MoveResult renameNoReplace(const std::string& from, const std::string& to);

// Moves path out of the way under the first free name among path + suffix,
// path + suffix + ".1" ... path + suffix + "." + maxNumbered, so that
// something set aside earlier is never lost. Returns the name used, or an
// empty string, with path untouched, if every name is taken or the move
// failed.
[[nodiscard]] std::string setAside(const std::string& path, std::string_view suffix = ".bad",
                                   unsigned maxNumbered = 9);

// Removes temporary files that a replaceFile(path, ...) cut short by a crash
// left beside path. Only regular files that carry replaceFile's own temporary
// name and have not been written for minAgeSeconds are removed: a file of the
// user's with a similar name is not ours to delete, and a fresh temporary may
// be another running copy's save in progress. Returns how many were removed.
// File system errors are not reported; whatever could not be removed stays.
std::size_t removeStaleTemporaries(std::string_view path, long long minAgeSeconds = 60);

}  // namespace pinball
