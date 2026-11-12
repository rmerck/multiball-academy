#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace pinball {

using Sha256Digest = std::array<std::uint8_t, 32>;

// Incremental SHA-256 (FIPS 180-4). Used to check asset files against the
// digests compiled into the program.
class Sha256 {
 public:
  Sha256() noexcept;

  // Adds bytes to the message.
  void update(std::span<const std::uint8_t> data) noexcept;
  // The same for a pointer and a length. Null data adds nothing.
  void update(const std::uint8_t* data, std::size_t size) noexcept {
    if (data != nullptr) {
      update(std::span<const std::uint8_t>(data, size));
    }
  }

  // Completes the hash. The object must not be used again afterwards.
  [[nodiscard]] Sha256Digest finish() noexcept;

 private:
  void compress(std::span<const std::uint8_t, 64> block) noexcept;

  std::array<std::uint32_t, 8> state_;
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffered_ = 0;
  std::uint64_t totalBytes_ = 0;
};

// One-shot convenience.
[[nodiscard]] Sha256Digest sha256(std::span<const std::uint8_t> data) noexcept;
// The same for a pointer and a length. Null data is the empty message.
[[nodiscard]] inline Sha256Digest sha256(const std::uint8_t* data, std::size_t size) noexcept {
  return data != nullptr ? sha256(std::span<const std::uint8_t>(data, size))
                         : sha256(std::span<const std::uint8_t>{});
}

}  // namespace pinball
