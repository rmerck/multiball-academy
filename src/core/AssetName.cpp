#include "core/AssetName.hpp"

#include <cstddef>

namespace pinball {

namespace {

constexpr std::size_t kMaxAssetNameLength = 128;
constexpr std::string_view kExtension = ".png";

// Names Windows treats as devices whatever the directory or extension.
bool isReservedStem(std::string_view stem) noexcept {
  for (const std::string_view reserved : {"con", "prn", "aux", "nul"}) {
    if (stem == reserved) {
      return true;
    }
  }
  if (stem.size() == 4 && (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt")) {
    return stem[3] >= '0' && stem[3] <= '9';
  }
  return false;
}

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
      // "..", hidden files, and components with a trailing dot.
      if (component.empty() || component.front() == '.' || component.back() == '.') {
        return false;
      }
      // Two dots in a row are never needed and are refused wherever they sit.
      if (component.find("..") != std::string_view::npos) {
        return false;
      }
      // Portability: refuse names Windows aliases to devices.
      if (isReservedStem(component.substr(0, component.find('.')))) {
        return false;
      }
      componentStart = i + 1;
    }
  }
  return true;
}

}  // namespace pinball
