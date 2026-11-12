#include "core/AssetManifest.hpp"

#include <algorithm>

namespace pinball {

AssetCheck checkAsset(const AssetEntry& entry, std::span<const std::uint8_t> data) noexcept {
  if (data.size() != entry.size) {
    return AssetCheck::WrongSize;
  }
  const Sha256Digest actual = sha256(data);
  // The digests are public, so an ordinary comparison is fine here.
  return actual == entry.sha256 ? AssetCheck::Ok : AssetCheck::WrongDigest;
}

const AssetEntry* findAsset(std::span<const AssetEntry> entries, std::string_view name) noexcept {
  const auto found = std::ranges::find(entries, name, &AssetEntry::name);
  return found != entries.end() ? &*found : nullptr;
}

}  // namespace pinball
