#include "core/HighScores.hpp"

#include <algorithm>
#include <charconv>
#include <concepts>
#include <system_error>

#include "core/Missions.hpp"
#include "core/Rules.hpp"

namespace pinball {

namespace {

// Version 2 added initials. Version 1 files are still read; their entries
// get dashes.
constexpr std::string_view kHeader = "SPACE-CADET-PINBALL-SCORES 2";
constexpr std::string_view kHeaderV1 = "SPACE-CADET-PINBALL-SCORES 1";

// Reads an unsigned decimal number made only of digits, with no sign, space
// or leading zero (other than "0" itself).
template <std::unsigned_integral T>
bool parseNumber(std::string_view text, T& out) noexcept {
  if (text.empty() || text.size() > 20 || (text.size() > 1 && text.front() == '0')) {
    return false;
  }
  if (!std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; })) {
    return false;
  }
  const char* const end = text.data() + text.size();
  const auto result = std::from_chars(text.data(), end, out, 10);
  return result.ec == std::errc{} && result.ptr == end;
}

}  // namespace

bool HighScores::qualifies(std::uint64_t score) const noexcept {
  if (score == 0) {
    return false;
  }
  return count_ < kCapacity || score > entries_[count_ - 1].score;
}

std::size_t HighScores::submit(HighScore entry) noexcept {
  if (!qualifies(entry.score)) {
    return kCapacity;
  }
  if (entry.score > points::kMaxScore) {
    entry.score = points::kMaxScore;
  }
  if (entry.rank >= kRankCount) {
    entry.rank = static_cast<std::uint8_t>(kRankCount - 1);
  }
  std::ranges::replace_if(entry.initials, [](char c) { return !isInitialsChar(c); }, '-');
  // Find the slot: after any equal scores, so earlier games keep their place.
  std::size_t position = 0;
  while (position < count_ && entries_[position].score >= entry.score) {
    ++position;
  }
  if (position >= kCapacity) {
    return kCapacity;
  }
  const std::size_t last = count_ < kCapacity ? count_ : kCapacity - 1;
  for (std::size_t i = last; i > position; --i) {
    entries_[i] = entries_[i - 1];
  }
  entries_[position] = entry;
  if (count_ < kCapacity) {
    ++count_;
  }
  return position;
}

std::string HighScores::serialize() const {
  std::string text(kHeader);
  text.push_back('\n');
  for (std::size_t i = 0; i < count_; ++i) {
    text += std::to_string(entries_[i].score);
    text.push_back(' ');
    text += std::to_string(static_cast<unsigned>(entries_[i].rank));
    text.push_back(' ');
    text.append(entries_[i].initials.data(), entries_[i].initials.size());
    text.push_back('\n');
  }
  return text;
}

bool HighScores::parse(std::string_view text) noexcept {
  entries_ = {};
  count_ = 0;
  if (text.size() > kMaxTextBytes) {
    return false;
  }

  std::array<HighScore, kCapacity> parsed{};
  std::size_t parsedCount = 0;
  bool sawHeader = false;
  bool hasInitials = false;
  while (!text.empty()) {
    const std::size_t newline = text.find('\n');
    if (newline == std::string_view::npos) {
      return false;  // Every line, including the last, ends with a newline.
    }
    const std::string_view line = text.substr(0, newline);
    text.remove_prefix(newline + 1);

    if (!sawHeader) {
      if (line != kHeader && line != kHeaderV1) {
        return false;
      }
      hasInitials = line == kHeader;
      sawHeader = true;
      continue;
    }
    if (parsedCount == kCapacity) {
      return false;
    }
    const std::size_t space = line.find(' ');
    if (space == std::string_view::npos) {
      return false;
    }
    std::string_view rankText = line.substr(space + 1);
    Initials initials = kNoInitials;
    if (hasInitials) {
      // Exactly one more field: three initials characters.
      const std::size_t second = rankText.find(' ');
      if (second == std::string_view::npos || rankText.size() - second - 1 != initials.size()) {
        return false;
      }
      for (std::size_t i = 0; i < initials.size(); ++i) {
        const char c = rankText[second + 1 + i];
        if (!isInitialsChar(c)) {
          return false;
        }
        initials[i] = c;
      }
      rankText = rankText.substr(0, second);
    }
    std::uint64_t score = 0;
    unsigned rank = 0;
    if (!parseNumber(line.substr(0, space), score) || !parseNumber(rankText, rank)) {
      return false;
    }
    if (score == 0 || score > points::kMaxScore || rank >= kRankCount) {
      return false;
    }
    // Must already be in order, highest first.
    if (parsedCount > 0 && score > parsed[parsedCount - 1].score) {
      return false;
    }
    parsed[parsedCount++] = {score, static_cast<std::uint8_t>(rank), initials};
  }
  if (!sawHeader) {
    return false;
  }
  entries_ = parsed;
  count_ = parsedCount;
  return true;
}

}  // namespace pinball
