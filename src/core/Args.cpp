#include "core/Args.hpp"

#include <charconv>
#include <system_error>

namespace pinball {

std::optional<std::uint32_t> parseFrameLimit(std::string_view text) noexcept {
  if (text.empty()) {
    return std::nullopt;
  }
  std::uint32_t value = 0;
  const char* const end = text.data() + text.size();
  const auto result = std::from_chars(text.data(), end, value, 10);
  if (result.ec != std::errc{} || result.ptr != end) {
    return std::nullopt;
  }
  if (value < 1 || value > kMaxFrameLimit) {
    return std::nullopt;
  }
  return value;
}

}  // namespace pinball
