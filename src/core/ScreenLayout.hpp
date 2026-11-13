#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

// Where everything goes on the screen, as a pure function of the window's
// size and safe area. The renderer, the touch mapper, the snapshot tool and
// the art scripts all read the same rectangles.
//
// Units are logical canvas units. The playfield is 768 x 960 units at 1:1 in
// every layout; only its place on the canvas changes.
namespace pinball::screen {

struct Rect {
  float x;
  float y;
  float w;
  float h;
};

// Classic: table left, panel right, 1280 x 960. Stack: status strip, table
// and control deck from top to bottom, 768 wide and at least 1312 high.
enum class Composition : std::uint8_t { Classic, Stack };
// Desktop: keyboard, a fixed 1280 x 960 canvas. Touch: on-screen controls
// and a canvas that follows the window.
enum class Idiom : std::uint8_t { Desktop, Touch };

// A window, in points: its size and how far the safe area is inset on each
// side (notch, home indicator, rounded corners).
struct Surface {
  float width;
  float height;
  float safeTop;
  float safeBottom;
  float safeLeft;
  float safeRight;
};

struct Layout {
  Composition composition;
  Idiom idiom;
  // The logical canvas. It covers the whole surface, so the presentation
  // adds no bars of its own. Not whole numbers on a touch surface: round
  // when handing them to SDL_SetRenderLogicalPresentation.
  float canvasWidth;
  float canvasHeight;
  float pointsPerUnit;  // Surface points per canvas unit.
  Rect safe;            // Canvas units, as are all below.
  Rect playfield;       // Always 768 x 960, at whole units.
  Rect hud;             // Classic: the panel. Stack: the strip.
  Rect deck;            // Stack only, else empty.
  Rect dialog;          // Where the overlays are laid out.
  // Hit rectangles of the touch controls. Empty for Desktop.
  Rect pauseButton;
  Rect nudgeLeftButton;
  Rect centreButton;
  Rect nudgeRightButton;
  // Everything below the status display, left and right of the composition's
  // centre line, out to the canvas edges. The buttons lie inside the zones
  // and take precedence over them. Empty for Desktop.
  Rect flipperLeftZone;
  Rect flipperRightZone;
};

// Sizes of the two compositions, in units.
inline constexpr float kPlayfieldWidth = 768.0F;
inline constexpr float kPlayfieldHeight = 960.0F;
inline constexpr float kClassicWidth = 1280.0F;
inline constexpr float kClassicHeight = 960.0F;
inline constexpr float kStripHeight = 232.0F;
inline constexpr float kDeckMinHeight = 120.0F;
inline constexpr float kDeckMaxHeight = 416.0F;
inline constexpr float kStackWidth = kPlayfieldWidth;
inline constexpr float kStackMinHeight = kStripHeight + kPlayfieldHeight + kDeckMinHeight;

// The flat colour behind the composition on a touch screen (red, green,
// blue): gutters, the bands outside the safe area. The backdrop art tiles
// over it and must end in it.
inline constexpr std::array<std::uint8_t, 3> kBackdropRgb{10, 0, 26};

// The smallest side of a touch control in the Stack, in units. It is at
// least 44 points on every preset.
inline constexpr float kMinHitUnits = 96.0F;

// The layout for a window. Never fails: a surface that is not a usable size
// (zero, negative, not a number) is treated as a very small one, and insets
// that would leave almost nothing are scaled back. Desktop ignores the
// surface and returns desktopLayout().
//
// A touch surface gets whichever composition shows the larger playfield;
// Stack on a tie. The composition is placed inside the safe area.
[[nodiscard]] Layout computeLayout(const Surface& surface, Idiom idiom) noexcept;
// Classic on a 1280 x 960 canvas, with no touch controls.
[[nodiscard]] Layout desktopLayout() noexcept;

// Named surfaces for the snapshot tool and the tests. The inset values are
// typical figures, not a claim about any one device.
struct Preset {
  std::string_view name;
  Surface surface;
  Idiom idiom;
};
[[nodiscard]] std::span<const Preset> presets() noexcept;
// Null if there is no preset of that name.
[[nodiscard]] const Preset* findPreset(std::string_view name) noexcept;

}  // namespace pinball::screen

// The status strip of the Stack composition, relative to the strip's origin.
namespace pinball::strip {

inline constexpr screen::Rect kPauseButton{16.0F, 8.0F, 88.0F, 84.0F};
// What a touch on the pause button may land in: a little more than is drawn.
inline constexpr screen::Rect kPauseHit{8.0F, 0.0F, 104.0F, 100.0F};
inline constexpr screen::Rect kScoreWell{112.0F, 8.0F, 408.0F, 84.0F};
inline constexpr screen::Rect kStatusWell{528.0F, 8.0F, 224.0F, 84.0F};
inline constexpr screen::Rect kRankWell{16.0F, 100.0F, 264.0F, 76.0F};
inline constexpr screen::Rect kMissionWell{288.0F, 100.0F, 464.0F, 76.0F};
inline constexpr screen::Rect kMessageWell{16.0F, 184.0F, 736.0F, 40.0F};

}  // namespace pinball::strip

// The control deck of the Stack composition, relative to the deck's origin.
// The button row is at the top, next to the table; the rest is flipper area.
namespace pinball::deck {

inline constexpr screen::Rect kNudgeLeftButton{132.0F, 12.0F, 96.0F, 96.0F};
inline constexpr screen::Rect kCentreButton{244.0F, 12.0F, 280.0F, 96.0F};
inline constexpr screen::Rect kNudgeRightButton{540.0F, 12.0F, 96.0F, 96.0F};
// Where the flipper pads' art starts, below the button row.
inline constexpr float kPadTop = 120.0F;

}  // namespace pinball::deck
