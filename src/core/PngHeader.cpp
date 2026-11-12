#include "core/PngHeader.hpp"

#include <array>

namespace pinball {

namespace {

constexpr std::array<unsigned char, 16> kExpectedPrefix{
    0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n',  // signature
    0x00, 0x00, 0x00, 0x0D,                        // IHDR data length: 13
    'I',  'H', 'D', 'R'};

std::uint32_t readBigEndian32(const unsigned char* p) noexcept {
  return (static_cast<std::uint32_t>(p[0]) << 24U) | (static_cast<std::uint32_t>(p[1]) << 16U) |
         (static_cast<std::uint32_t>(p[2]) << 8U) | static_cast<std::uint32_t>(p[3]);
}

}  // namespace

std::optional<PngSize> parsePngHeader(const unsigned char* data, std::size_t size) noexcept {
  if (data == nullptr || size < kPngHeaderBytes) {
    return std::nullopt;
  }
  for (std::size_t i = 0; i < kExpectedPrefix.size(); ++i) {
    if (data[i] != kExpectedPrefix[i]) {
      return std::nullopt;
    }
  }
  const PngSize result{readBigEndian32(data + 16), readBigEndian32(data + 20)};
  // The PNG specification limits dimensions to 31 bits and forbids zero.
  constexpr std::uint32_t kPngMaxDimension = 0x7FFFFFFFU;
  if (result.width == 0 || result.height == 0 || result.width > kPngMaxDimension ||
      result.height > kPngMaxDimension) {
    return std::nullopt;
  }
  return result;
}

}  // namespace pinball
