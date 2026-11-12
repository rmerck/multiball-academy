#pragma once

#include <cstddef>

#include "core/AssetManifest.hpp"

namespace pinball {

// The asset manifest compiled into this executable at build time.
const AssetEntry* embeddedAssets() noexcept;
std::size_t embeddedAssetCount() noexcept;

}  // namespace pinball
