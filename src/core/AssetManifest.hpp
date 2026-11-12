#pragma once

#include <cstddef>
#include <cstdint>
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

// Compares a file's bytes with its manifest entry. data may be null only
// when size is 0.
AssetCheck checkAsset(const AssetEntry& entry, const std::uint8_t* data,
                      std::size_t size) noexcept;

// Finds name in a manifest. Returns nullptr if it is not listed.
const AssetEntry* findAsset(const AssetEntry* entries, std::size_t count,
                            std::string_view name) noexcept;

}  // namespace pinball
