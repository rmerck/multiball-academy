#include "core/Args.hpp"

#include <charconv>
#include <cstddef>
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

std::optional<Scene> parseScene(std::string_view name) noexcept {
  for (std::size_t i = 0; i < kSceneNames.size(); ++i) {
    if (name == kSceneNames[i]) {
      return static_cast<Scene>(i);
    }
  }
  return std::nullopt;
}

std::string_view sceneName(Scene scene) noexcept {
  const auto index = static_cast<std::size_t>(scene);
  return index < kSceneNames.size() ? kSceneNames[index] : std::string_view{};
}

}  // namespace pinball
