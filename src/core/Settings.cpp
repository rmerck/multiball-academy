#include "core/Settings.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <system_error>

namespace pinball {

namespace {

constexpr std::string_view kHeader = "PINBALL-SETTINGS 1";

// An unsigned decimal of digits only, no sign, space or leading zero, at
// most limit.
bool parseNumber(std::string_view text, std::uint32_t limit, std::uint32_t& out) noexcept {
  if (text.empty() || text.size() > 9 || (text.size() > 1 && text.front() == '0')) {
    return false;
  }
  if (!std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; })) {
    return false;
  }
  std::uint32_t value = 0;
  const char* const end = text.data() + text.size();
  const auto result = std::from_chars(text.data(), end, value, 10);
  if (result.ec != std::errc{} || result.ptr != end || value > limit) {
    return false;
  }
  out = value;
  return true;
}

bool parseFlag(std::string_view text, bool& out) noexcept {
  if (text == "1") {
    out = true;
    return true;
  }
  if (text == "0") {
    out = false;
    return true;
  }
  return false;
}

enum class Key : std::uint8_t { Sound, Volume, Music, Haptics, Guide, Games, Asked, Count };
constexpr std::array<std::string_view, static_cast<std::size_t>(Key::Count)> kKeys{
    "sound", "volume", "music", "haptics", "guide", "games", "asked"};

}  // namespace

bool isSettingsVersion(std::string_view text) noexcept {
  return !text.empty() && text.size() <= Settings::kMaxVersionLength &&
         std::ranges::all_of(text, [](char c) { return (c >= '0' && c <= '9') || c == '.'; });
}

std::string Settings::serialize() const {
  std::string text(kHeader);
  text += "\nsound ";
  text += sound ? '1' : '0';
  text += "\nvolume ";
  text += std::to_string(static_cast<unsigned>(std::min(volume, kMaxVolume)));
  text += "\nmusic ";
  text += music ? '1' : '0';
  text += "\nhaptics ";
  text += haptics ? '1' : '0';
  text += "\nguide ";
  text += touchGuideSeen ? '1' : '0';
  text += "\ngames ";
  text += std::to_string(std::min(gamesFinished, kMaxGames));
  text += "\nasked ";
  text += isSettingsVersion(reviewAskedVersion) ? std::string_view(reviewAskedVersion)
                                                : std::string_view("-");
  text += '\n';
  return text;
}

bool Settings::parse(std::string_view text) noexcept {
  *this = Settings{};
  if (text.size() > kMaxTextBytes) {
    return false;
  }
  Settings parsed;
  std::array<bool, kKeys.size()> seen{};
  bool sawHeader = false;
  while (!text.empty()) {
    const std::size_t newline = text.find('\n');
    if (newline == std::string_view::npos) {
      return false;  // Every line, the last too, ends with a newline.
    }
    const std::string_view line = text.substr(0, newline);
    text.remove_prefix(newline + 1);
    if (!sawHeader) {
      if (line != kHeader) {
        return false;
      }
      sawHeader = true;
      continue;
    }
    const std::size_t space = line.find(' ');
    if (space == std::string_view::npos) {
      return false;
    }
    const std::string_view name = line.substr(0, space);
    const std::string_view value = line.substr(space + 1);
    const auto found = std::ranges::find(kKeys, name);
    if (found == kKeys.end()) {
      // A key this version does not know, from a newer one: passed over, so
      // that the rest of the file still counts. Only a plain word with a
      // plain value (the file's size bounds how many).
      const auto word = [](char c) { return (c >= 'a' && c <= 'z') || c == '-'; };
      const auto plain = [](char c) { return c > ' ' && c <= '~'; };
      if (name.empty() || !std::ranges::all_of(name, word) || value.empty() ||
          !std::ranges::all_of(value, plain)) {
        return false;
      }
      continue;
    }
    const auto index = static_cast<std::size_t>(found - kKeys.begin());
    if (seen[index]) {
      return false;
    }
    seen[index] = true;
    bool ok = false;
    std::uint32_t number = 0;
    switch (static_cast<Key>(index)) {
      case Key::Sound:
        ok = parseFlag(value, parsed.sound);
        break;
      case Key::Volume:
        ok = parseNumber(value, kMaxVolume, number);
        parsed.volume = static_cast<std::uint8_t>(number);
        break;
      case Key::Music:
        ok = parseFlag(value, parsed.music);
        break;
      case Key::Haptics:
        ok = parseFlag(value, parsed.haptics);
        break;
      case Key::Guide:
        ok = parseFlag(value, parsed.touchGuideSeen);
        break;
      case Key::Games:
        ok = parseNumber(value, kMaxGames, number);
        parsed.gamesFinished = number;
        break;
      case Key::Asked:
        if (value == "-") {
          ok = true;
        } else if (isSettingsVersion(value)) {
          ok = true;
          parsed.reviewAskedVersion.assign(value);
        }
        break;
      case Key::Count:
        break;
    }
    if (!ok) {
      return false;
    }
  }
  if (!sawHeader) {
    return false;
  }
  *this = std::move(parsed);
  return true;
}

}  // namespace pinball
