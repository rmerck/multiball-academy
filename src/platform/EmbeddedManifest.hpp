#pragma once

#include <span>

#include "core/AssetManifest.hpp"

namespace pinball {

// The asset manifest compiled into this executable at build time.
[[nodiscard]] std::span<const AssetEntry> embeddedAssets() noexcept;

}  // namespace pinball
