#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace pinball {

inline constexpr std::uint32_t kMaxFrameLimit = 1'000'000;

// Parses a decimal frame count in [1, kMaxFrameLimit]. Only the digits 0-9 are
// accepted (leading zeros included). Rejects signs, whitespace, any other
// character and out-of-range values.
std::optional<std::uint32_t> parseFrameLimit(std::string_view text) noexcept;

}  // namespace pinball
