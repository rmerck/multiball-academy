#include "platform/EmbeddedManifest.hpp"

#include <iterator>

namespace pinball {

namespace {

const AssetEntry kEntries[] = {
#include "AssetManifest.inc"
};

}  // namespace

const AssetEntry* embeddedAssets() noexcept { return kEntries; }
std::size_t embeddedAssetCount() noexcept { return std::size(kEntries); }

}  // namespace pinball
