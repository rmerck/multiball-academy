#pragma once

#include <string_view>

namespace pinball {

// True if name is a safe relative asset path: lower-case letters, digits,
// '_', '-', '.', and '/' separators, ending in ".png", with no empty, "." or
// ".." components and no leading or trailing separator. Asset names are
// compile-time constants today; this keeps any future caller from escaping
// the asset directory.
bool isSafeAssetName(std::string_view name) noexcept;

}  // namespace pinball
