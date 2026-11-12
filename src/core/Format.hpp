#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace pinball {

// Enough for the largest 64-bit value with separators, plus a terminator.
using ScoreText = std::array<char, 28>;

// Writes value in decimal with a comma every three digits ("1,234,567") into
// buffer and returns a view of the text. Never writes outside buffer.
std::string_view formatScore(std::uint64_t value, ScoreText& buffer) noexcept;

// printf-style formatting into a caller-supplied buffer. The result is
// truncated to fit and always terminated; the returned view covers what was
// written. The format string is checked against the arguments at compile time.
#if defined(__GNUC__) || defined(__clang__)
#define PINBALL_PRINTF_LIKE(formatIndex, firstArg) \
  __attribute__((format(printf, formatIndex, firstArg)))
#else
#define PINBALL_PRINTF_LIKE(formatIndex, firstArg)
#endif
PINBALL_PRINTF_LIKE(3, 4)
std::string_view formatText(char* buffer, std::size_t size, const char* pattern, ...) noexcept;

}  // namespace pinball
