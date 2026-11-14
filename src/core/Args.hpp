#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace pinball {

inline constexpr std::uint32_t kMaxFrameLimit = 1'000'000;

// Parses a decimal frame count in [1, kMaxFrameLimit]. Only the digits 0-9 are
// accepted (leading zeros included). Rejects signs, whitespace, any other
// character and out-of-range values.
[[nodiscard]] std::optional<std::uint32_t> parseFrameLimit(std::string_view text) noexcept;

// Moments of a game for the store's screenshots, set up from a new game by
// playing them through: the rules hear the same events a player's shots
// would make, and the table moves the balls as it always does. The same
// scene always comes out the same.
enum class Scene : std::uint8_t {
  Table,       // A new game: the ball on the plunger.
  Multiball,   // Academy multiball: three balls in play, jackpots lit.
  Hangar,      // Two balls locked, the bay doors down, the third on the plunger.
  Course,      // A course running, its clock going, one shot of it made.
  Graduation,  // Graduation Day: every course passed, three balls in play.
};

inline constexpr std::array<std::string_view, 5> kSceneNames{"table", "multiball", "hangar",
                                                             "course", "graduation"};

// The scene with this name (as kSceneNames spells it), if there is one.
[[nodiscard]] std::optional<Scene> parseScene(std::string_view name) noexcept;
[[nodiscard]] std::string_view sceneName(Scene scene) noexcept;

}  // namespace pinball
