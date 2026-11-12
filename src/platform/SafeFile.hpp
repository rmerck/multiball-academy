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

// Reads a whole regular file of at most maxBytes into out.
//
// The file is opened once and every check is made on that open handle, so
// what is checked is what is read. A symbolic link at the path is refused,
// and a pipe or device there cannot block the caller. At most maxBytes + 1
// bytes are ever buffered. out is empty unless the result is Ok.
ReadResult readRegularFile(const std::string& path, std::size_t maxBytes,
                           std::vector<std::uint8_t>& out);

// Replaces the file at path with text, all or nothing: the text is written
// to a new private file beside it, flushed to disk, and renamed into place.
// A link or leftover at the temporary name is never written through.
// Returns false, leaving any existing file untouched, on failure.
bool replaceFile(const std::string& path, std::string_view text);

}  // namespace pinball
