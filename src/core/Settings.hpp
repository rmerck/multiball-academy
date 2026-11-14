#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace pinball {

// The player's choices and the few counts the game keeps between runs,
// kept in a small file beside the high scores. Knows how to turn itself
// into text and back; reading and writing the file is the platform's job
// (platform/SettingsStore.hpp).
struct Settings {
  static constexpr std::uint8_t kMaxVolume = 10;
  // A finished game count stops here; nothing needs more.
  static constexpr std::uint32_t kMaxGames = 999'999;
  // The longest version string kept for the review prompt.
  static constexpr std::size_t kMaxVersionLength = 15;
  // Largest file text accepted by parse. A valid file is far smaller.
  static constexpr std::size_t kMaxTextBytes = 256;

  bool sound = true;
  // 0 (silent) to kMaxVolume (full).
  std::uint8_t volume = 8;
  bool music = true;
  bool haptics = true;
  // The touch guide was shown once and dismissed.
  bool touchGuideSeen = false;
  // Games played to the end, by a player, on this device.
  std::uint32_t gamesFinished = 0;
  // The version for which a store review was last asked for, or empty.
  std::string reviewAskedVersion;

  [[nodiscard]] bool operator==(const Settings&) const = default;

  // The volume as a gain, 0 to 1.
  [[nodiscard]] float gain() const noexcept {
    return static_cast<float>(volume) / static_cast<float>(kMaxVolume);
  }

  // Text form: a header line, then one "key value" line for each setting.
  [[nodiscard]] std::string serialize() const;
  // Replaces every setting with what text says. A key that is missing
  // keeps its default. Anything malformed, unknown, repeated or out of
  // range makes the whole text invalid: the settings are left at their
  // defaults and false is returned. Never trusts the input.
  [[nodiscard]] bool parse(std::string_view text) noexcept;
};

// A version string as the settings keep it: digits and dots, one to
// kMaxVersionLength characters.
[[nodiscard]] bool isSettingsVersion(std::string_view text) noexcept;

}  // namespace pinball
