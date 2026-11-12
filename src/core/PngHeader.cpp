#include "core/PngHeader.hpp"

#include <algorithm>
#include <array>

namespace pinball {

namespace {

constexpr std::array<std::uint8_t, 16> kExpectedPrefix{
    0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n',  // signature
    0x00, 0x00, 0x00, 0x0D,                        // IHDR data length: 13
    'I',  'H', 'D', 'R'};
static_assert(kExpectedPrefix.size() + 8 == kPngHeaderBytes, "prefix, then width and height");

constexpr std::uint32_t readBigEndian32(std::span<const std::uint8_t, 4> p) noexcept {
  return (static_cast<std::uint32_t>(p[0]) << 24U) | (static_cast<std::uint32_t>(p[1]) << 16U) |
         (static_cast<std::uint32_t>(p[2]) << 8U) | static_cast<std::uint32_t>(p[3]);
}

}  // namespace

std::optional<PngSize> parsePngHeader(std::span<const std::uint8_t> data) noexcept {
  if (data.size() < kPngHeaderBytes) {
    return std::nullopt;
  }
  if (!std::ranges::equal(data.first(kExpectedPrefix.size()), kExpectedPrefix)) {
    return std::nullopt;
  }
  const PngSize result{readBigEndian32(data.subspan<16, 4>()),
                       readBigEndian32(data.subspan<20, 4>())};
  // The PNG specification limits dimensions to 31 bits and forbids zero.
  constexpr std::uint32_t kPngMaxDimension = 0x7FFFFFFFU;
  if (result.width == 0 || result.height == 0 || result.width > kPngMaxDimension ||
      result.height > kPngMaxDimension) {
    return std::nullopt;
  }
  return result;
}

}  // namespace pinball
