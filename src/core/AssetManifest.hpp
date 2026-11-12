#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "core/Sha256.hpp"

namespace pinball {

// One asset the program is allowed to load, with the exact size and SHA-256
// it had when the program was built.
struct AssetEntry {
  std::string_view name;
  std::uint64_t size;
  Sha256Digest sha256;
};

enum class AssetCheck : std::uint8_t { Ok, WrongSize, WrongDigest };

// Compares a file's bytes with its manifest entry.
[[nodiscard]] AssetCheck checkAsset(const AssetEntry& entry,
                                    std::span<const std::uint8_t> data) noexcept;
// The same for a pointer and a length. Null data is an empty file; null
// data with a length is never a match.
[[nodiscard]] inline AssetCheck checkAsset(const AssetEntry& entry, const std::uint8_t* data,
                                           std::size_t size) noexcept {
  if (data == nullptr) {
    return size == 0 ? checkAsset(entry, std::span<const std::uint8_t>{}) : AssetCheck::WrongSize;
  }
  return checkAsset(entry, std::span<const std::uint8_t>(data, size));
}

// Finds name in a manifest. Returns nullptr if it is not listed.
[[nodiscard]] const AssetEntry* findAsset(std::span<const AssetEntry> entries,
                                          std::string_view name) noexcept;
// The same for a pointer and a count. A null manifest lists nothing.
[[nodiscard]] inline const AssetEntry* findAsset(const AssetEntry* entries, std::size_t count,
                                                 std::string_view name) noexcept {
  return entries != nullptr ? findAsset(std::span<const AssetEntry>(entries, count), name)
                            : nullptr;
}

}  // namespace pinball
