#include "core/Format.hpp"

#include <cstdarg>
#include <cstddef>
#include <cstdio>

namespace pinball {

std::string_view formatScore(std::uint64_t value, ScoreText& buffer) noexcept {
  // Build from the right-hand end backwards.
  std::size_t pos = buffer.size();
  int digits = 0;
  do {
    if (digits > 0 && digits % 3 == 0) {
      buffer[--pos] = ',';
    }
    buffer[--pos] = static_cast<char>('0' + value % 10U);
    value /= 10U;
    ++digits;
    // 20 digits and 6 separators at most: pos cannot reach 0.
  } while (value != 0U);
  return {buffer.data() + pos, buffer.size() - pos};
}

std::string_view formatText(char* buffer, std::size_t size, const char* pattern, ...) noexcept {
  if (buffer == nullptr || size == 0 || pattern == nullptr) {
    return {};
  }
  va_list args;
  va_start(args, pattern);
  const int written = std::vsnprintf(buffer, size, pattern, args);
  va_end(args);
  if (written <= 0) {
    buffer[0] = '\0';
    return {};
  }
  const auto length = static_cast<std::size_t>(written);
  return {buffer, length < size ? length : size - 1};
}

}  // namespace pinball
