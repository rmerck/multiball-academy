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
// blue): gutters, the bands outside the safe area, the desktop's letterbox
// and the dimming behind the dialogs. The backdrop art tiles over it and
// must end in it. It is the art's NIGHT (tools/art/palette.py; the renderer
// asserts the two agree) and the iOS launch screen's colour
// (packaging/ios/Assets.xcassets/LaunchBackground.colorset).
inline constexpr std::array<std::uint8_t, 3> kBackdropRgb{18, 18, 58};

// The smallest side of what a touch on a control may land in, in units, in
// both compositions and in the overlays.
inline constexpr float kMinHitUnits = 96.0F;
// The size that must come to in points for a finger to find it.
inline constexpr float kMinHitPoints = 44.0F;
// The smallest window the game should be given, as the size of its safe
// area in points: at this size and above, kMinHitUnits is at least
// kMinHitPoints whatever the shape (the Stack composition fits, and a
// composition is only ever replaced by one that draws larger). The platform
// asks the system for no smaller a scene; a smaller window still gets a
// correct picture, with smaller controls.
inline constexpr float kMinSceneWidth = 352.0F;
inline constexpr float kMinSceneHeight = 602.0F;
// True if the surface's safe area is at least the smallest scene.
[[nodiscard]] bool meetsMinimumScene(const Surface& surface) noexcept;

// Text. The display's font is seven font pixels high and is drawn at a
// whole number of units to the font pixel, its "size". A line should be at
// least kMinTextPoints high on the screen: the capitals of 8 point type,
// about what the smallest system captions use.
inline constexpr float kFontRows = 7.0F;
inline constexpr float kMinTextPoints = 8.0F;
// The smallest size the dialogs draw at in this layout: 2 where that is
// kMinTextPoints or more (a desktop, a tablet), else 3, which is enough
// down to the smallest scene.
[[nodiscard]] constexpr float smallestTextSize(const Layout& layout) noexcept {
  return 2.0F * kFontRows * layout.pointsPerUnit >= kMinTextPoints ? 2.0F : 3.0F;
}
// The smallest size the status display draws at: the Stack's strip, which
// phones show, is all at 3; the Classic panel has lines at 2.
[[nodiscard]] constexpr float hudTextSize(const Layout& layout) noexcept {
  return layout.composition == Composition::Stack ? 3.0F : 2.0F;
}

// The layout for a window. Never fails: a side that is not a usable size
// (under kMinSurfacePoints, negative, not a number) counts as
// kMinSurfacePoints, one over kMaxSurfacePoints as that, and insets that
// would leave almost nothing are scaled back. So the canvas is never more
// than kMaxCanvasUnits on a side, and whole units stay exact. Desktop ignores the
// surface and returns desktopLayout().
//
// A touch surface gets whichever composition shows the larger playfield;
// Stack on a tie. The composition is placed inside the safe area.
[[nodiscard]] Layout computeLayout(const Surface& surface, Idiom idiom) noexcept;
// Classic on a 1280 x 960 canvas, with no touch controls.
[[nodiscard]] Layout desktopLayout() noexcept;

// Bounds on what is believed of a surface's sides, in points; the share of a
// side its two insets may take between them; and what those hold the canvas
// to, in units: small enough that whole units stay exact.
inline constexpr float kMinSurfacePoints = 64.0F;
inline constexpr float kMaxSurfacePoints = 16384.0F;
inline constexpr float kMaxInsetShare = 0.75F;
inline constexpr float kMaxCanvasUnits =
    kMaxSurfacePoints * kStackMinHeight / (kMinSurfacePoints * (1.0F - kMaxInsetShare));
static_assert(kMaxCanvasUnits < 16'777'216.0F);

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
// The pads also come up beside the row, into the two corners the buttons
// leave free: on a short deck (a 16:9 phone) those corners are the whole
// pad, and they keep the thumbs below the table and off the buttons.
// A clear gap is left between a corner and the nudge button next to it.
inline constexpr float kCornerWidth = 108.0F;
inline constexpr screen::Rect kLeftCorner{8.0F, 12.0F, kCornerWidth - 8.0F, 96.0F};
inline constexpr screen::Rect kRightCorner{screen::kStackWidth - kCornerWidth, 12.0F,
                                           kCornerWidth - 8.0F, 96.0F};

}  // namespace pinball::deck
