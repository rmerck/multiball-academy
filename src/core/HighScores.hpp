#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace pinball {

// A player's three initials: capital letters, or dashes when none were given.
using Initials = std::array<char, 3>;
inline constexpr Initials kNoInitials{'-', '-', '-'};
constexpr bool isInitialsChar(char c) noexcept { return (c >= 'A' && c <= 'Z') || c == '-'; }

struct HighScore {
  std::uint64_t score = 0;
  std::uint8_t rank = 0;  // Rank index reached, 0 (the first) to 8.
  Initials initials = kNoInitials;
};

// The best games on this machine, highest first. Knows how to turn itself
// into text and back; reading and writing the file is the platform's job.
class HighScores {
 public:
  static constexpr std::size_t kCapacity = 5;
  // Largest file text accepted by parse. A valid table is far smaller.
  static constexpr std::size_t kMaxTextBytes = 512;

  // Number of entries, 0 to kCapacity.
  std::size_t size() const noexcept { return count_; }
  // Entry at position, or an empty entry if out of range.
  HighScore at(std::size_t position) const noexcept {
    return position < count_ ? entries_[position] : HighScore{};
  }
  std::uint64_t best() const noexcept { return count_ > 0 ? entries_[0].score : 0; }

  // True if score would enter the table.
  [[nodiscard]] bool qualifies(std::uint64_t score) const noexcept;
  // Adds an entry if it qualifies, keeping the table sorted. Returns its
  // position, or kCapacity if it did not make the table. Zero never does.
  std::size_t submit(HighScore entry) noexcept;

  // Text form: a header line then one "score rank initials" line per entry.
  [[nodiscard]] std::string serialize() const;
  // Replaces the table with what text describes. Anything malformed, out of
  // range or out of order makes the whole text invalid: the table is left
  // empty and false is returned. Never trusts the input.
  [[nodiscard]] bool parse(std::string_view text) noexcept;

 private:
  std::array<HighScore, kCapacity> entries_{};
  std::size_t count_ = 0;
};

}  // namespace pinball
