#include "core/AssetName.hpp"

#include <cstddef>

namespace pinball {

namespace {

constexpr std::size_t kMaxAssetNameLength = 128;
constexpr std::string_view kExtension = ".png";

bool isAllowedChar(char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
         c == '/';
}

}  // namespace

bool isSafeAssetName(std::string_view name) noexcept {
  if (name.size() <= kExtension.size() || name.size() > kMaxAssetNameLength) {
    return false;
  }
  if (name.substr(name.size() - kExtension.size()) != kExtension) {
    return false;
  }

  std::size_t componentStart = 0;
  for (std::size_t i = 0; i <= name.size(); ++i) {
    if (i < name.size() && !isAllowedChar(name[i])) {
      return false;
    }
    if (i == name.size() || name[i] == '/') {
      const std::string_view component = name.substr(componentStart, i - componentStart);
      // Rejects empty components (leading, trailing or doubled '/'), "." and
      // "..", and hidden files.
      if (component.empty() || component.front() == '.') {
        return false;
      }
      componentStart = i + 1;
    }
  }
  return true;
}

}  // namespace pinball
