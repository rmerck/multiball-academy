#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace pinball {

using Sha256Digest = std::array<std::uint8_t, 32>;

// Incremental SHA-256 (FIPS 180-4). Used to check asset files against the
// digests compiled into the program.
class Sha256 {
 public:
  Sha256() noexcept;

  // Adds size bytes. data may be null only when size is 0.
  void update(const std::uint8_t* data, std::size_t size) noexcept;

  // Completes the hash. The object must not be used again afterwards.
  Sha256Digest finish() noexcept;

 private:
  void compress(const std::uint8_t* block) noexcept;

  std::array<std::uint32_t, 8> state_;
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffered_ = 0;
  std::uint64_t totalBytes_ = 0;
};

// One-shot convenience.
Sha256Digest sha256(const std::uint8_t* data, std::size_t size) noexcept;

}  // namespace pinball
