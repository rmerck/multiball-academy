#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace pinball {

inline constexpr std::uint32_t kMaxFrameLimit = 1'000'000;

// Parses a strictly decimal frame count in [1, kMaxFrameLimit]. Rejects signs,
// whitespace, trailing characters and out-of-range values.
std::optional<std::uint32_t> parseFrameLimit(std::string_view text) noexcept;

}  // namespace pinball
