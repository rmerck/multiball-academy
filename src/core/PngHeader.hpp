#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace pinball {

struct PngSize {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

// Number of leading bytes of a PNG file needed by parsePngHeader.
inline constexpr std::size_t kPngHeaderBytes = 24;

// Reads the image size from the first bytes of a PNG file without decoding
// it: the 8-byte signature followed by an IHDR chunk. Returns nullopt if the
// data is too short, is not a PNG, or declares a zero dimension.
std::optional<PngSize> parsePngHeader(const unsigned char* data, std::size_t size) noexcept;

}  // namespace pinball
