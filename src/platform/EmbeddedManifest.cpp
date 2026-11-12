#include "platform/EmbeddedManifest.hpp"

namespace pinball {

namespace {

const AssetEntry kEntries[] = {
#include "AssetManifest.inc"
};

}  // namespace

std::span<const AssetEntry> embeddedAssets() noexcept { return kEntries; }

}  // namespace pinball
