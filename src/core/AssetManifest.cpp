#include "core/AssetManifest.hpp"

namespace pinball {

AssetCheck checkAsset(const AssetEntry& entry, const std::uint8_t* data,
                      std::size_t size) noexcept {
  if (static_cast<std::uint64_t>(size) != entry.size) {
    return AssetCheck::WrongSize;
  }
  const Sha256Digest actual = sha256(data, size);
  // The digests are public, so an ordinary comparison is fine here.
  return actual == entry.sha256 ? AssetCheck::Ok : AssetCheck::WrongDigest;
}

const AssetEntry* findAsset(const AssetEntry* entries, std::size_t count,
                            std::string_view name) noexcept {
  if (entries == nullptr) {
    return nullptr;
  }
  for (std::size_t i = 0; i < count; ++i) {
    if (entries[i].name == name) {
      return &entries[i];
    }
  }
  return nullptr;
}

}  // namespace pinball
