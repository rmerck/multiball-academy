#include "core/ScreenLayout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string_view>

#include "core/PanelLayout.hpp"
#include "core/TableLayout.hpp"

namespace pinball::screen {

// The playfield of the layouts is the table itself.
static_assert(kPlayfieldWidth == table::kWidth && kPlayfieldHeight == table::kHeight);
static_assert(kClassicWidth == panel::kLeft + panel::kWidth && kClassicHeight == panel::kHeight);

namespace {

constexpr Rect kNone{0.0F, 0.0F, 0.0F, 0.0F};
// Today's About box on the Classic canvas; the Stack's dialog sits over the
// table, this far in from the column's edge and below the strip.
constexpr Rect kClassicDialog{190.0F, 90.0F, 900.0F, 780.0F};
constexpr float kStackDialogInset = 24.0F;
constexpr float kStackDialogDrop = 40.0F;
constexpr float kStackDialogHeight = 880.0F;

// Bounds on what is believed of a surface, in points.
constexpr float kMinSurface = 1.0F;
constexpr float kMaxSurface = 16384.0F;
// Insets may take at most this share of a side between them.
constexpr float kMaxInsetShare = 0.75F;

constexpr Rect moved(const Rect& r, float dx, float dy) noexcept {
  return {r.x + dx, r.y + dy, r.w, r.h};
}

float usable(float value, float fallback) noexcept {
  return std::isfinite(value) ? value : fallback;
}

// A surface with a size in range and insets that are not negative and leave
// at least a quarter of each side.
Surface sanitised(const Surface& s) noexcept {
  Surface out{};
  out.width = std::clamp(usable(s.width, kMinSurface), kMinSurface, kMaxSurface);
  out.height = std::clamp(usable(s.height, kMinSurface), kMinSurface, kMaxSurface);
  const auto fit = [](float a, float b, float side, float& outA, float& outB) noexcept {
    a = std::clamp(usable(a, 0.0F), 0.0F, kMaxSurface);
    b = std::clamp(usable(b, 0.0F), 0.0F, kMaxSurface);
    const float limit = side * kMaxInsetShare;
    if (a + b > limit) {
      const float scale = limit / (a + b);
      a *= scale;
      b *= scale;
    }
    outA = a;
    outB = b;
  };
  fit(s.safeLeft, s.safeRight, out.width, out.safeLeft, out.safeRight);
  fit(s.safeTop, s.safeBottom, out.height, out.safeTop, out.safeBottom);
  return out;
}

// Where a block of the given extent starts so that it sits at whole units
// and stays between low and high. wanted is where it would go unrounded.
// When the block fills the range exactly it cannot always be both: then it
// starts at the whole unit at or before low, which is never off the canvas.
float snapped(float wanted, float extent, float low, float high) noexcept {
  // The range is a product of floats and can come out a hair short.
  constexpr float kSlack = 1.0F / 64.0F;
  float start = std::floor(wanted + 0.5F);
  if (start < low - kSlack) {
    start = std::ceil(wanted);
  }
  if (start + extent > high + kSlack) {
    start = std::floor(wanted);
  }
  return std::max(start, std::floor(low));
}

Layout classicTouch(Layout layout) noexcept {
  const Rect& safe = layout.safe;
  const float x =
      snapped(safe.x + (safe.w - kClassicWidth) * 0.5F, kClassicWidth, safe.x, safe.x + safe.w);
  const float y =
      snapped(safe.y + (safe.h - kClassicHeight) * 0.5F, kClassicHeight, safe.y, safe.y + safe.h);
  layout.composition = Composition::Classic;
  layout.playfield = {x, y, kPlayfieldWidth, kPlayfieldHeight};
  layout.hud = {x + panel::kLeft, y, panel::kWidth, panel::kHeight};
  layout.deck = kNone;
  layout.dialog = moved(kClassicDialog, x, y);
  layout.pauseButton = moved(panel::kPauseButton, x, y);
  layout.nudgeLeftButton = moved(panel::kNudgeLeftButton, x, y);
  layout.centreButton = moved(panel::kCentreButton, x, y);
  layout.nudgeRightButton = moved(panel::kNudgeRightButton, x, y);
  // Split down the middle of the composition, the full height of the canvas.
  const float split = x + kClassicWidth * 0.5F;
  layout.flipperLeftZone = {0.0F, 0.0F, split, layout.canvasHeight};
  layout.flipperRightZone = {split, 0.0F, layout.canvasWidth - split, layout.canvasHeight};
  return layout;
}

Layout stack(Layout layout) noexcept {
  const Rect& safe = layout.safe;
  const float safeBottom = safe.y + safe.h;
  // The deck takes all the spare height up to its cap; what is left over
  // goes above the strip, which pushes the content down toward the thumbs.
  const float spare = std::max(safe.h - kStripHeight - kPlayfieldHeight, kDeckMinHeight);
  const float spacer = spare - std::min(spare, kDeckMaxHeight);
  const float blocks = kStripHeight + kPlayfieldHeight + kDeckMinHeight;
  const float x =
      snapped(safe.x + (safe.w - kStackWidth) * 0.5F, kStackWidth, safe.x, safe.x + safe.w);
  const float y = snapped(safe.y + spacer, blocks, safe.y, safeBottom);
  layout.composition = Composition::Stack;
  layout.hud = {x, y, kStackWidth, kStripHeight};
  layout.playfield = {x, y + kStripHeight, kPlayfieldWidth, kPlayfieldHeight};
  const float deckTop = y + kStripHeight + kPlayfieldHeight;
  layout.deck = {x, deckTop, kStackWidth,
                 std::clamp(safeBottom - deckTop, kDeckMinHeight, kDeckMaxHeight)};
  layout.dialog = {x + kStackDialogInset, layout.playfield.y + kStackDialogDrop,
                   kStackWidth - 2.0F * kStackDialogInset, kStackDialogHeight};
  layout.pauseButton = moved(strip::kPauseHit, x, y);
  layout.nudgeLeftButton = moved(deck::kNudgeLeftButton, x, deckTop);
  layout.centreButton = moved(deck::kCentreButton, x, deckTop);
  layout.nudgeRightButton = moved(deck::kNudgeRightButton, x, deckTop);
  // From the bottom of the strip to the bottom edge of the screen, gutters
  // and the home-indicator band included.
  const float split = x + kStackWidth * 0.5F;
  const float top = y + kStripHeight;
  layout.flipperLeftZone = {0.0F, top, split, layout.canvasHeight - top};
  layout.flipperRightZone = {split, top, layout.canvasWidth - split, layout.canvasHeight - top};
  return layout;
}

constexpr std::array<Preset, 10> kPresets{{
    {"desktop", {kClassicWidth, kClassicHeight, 0.0F, 0.0F, 0.0F, 0.0F}, Idiom::Desktop},
    {"phone", {393.0F, 852.0F, 59.0F, 34.0F, 0.0F, 0.0F}, Idiom::Touch},
    {"phone-short", {375.0F, 667.0F, 20.0F, 0.0F, 0.0F, 0.0F}, Idiom::Touch},
    {"phone-max", {440.0F, 956.0F, 62.0F, 34.0F, 0.0F, 0.0F}, Idiom::Touch},
    {"duo-outer", {466.0F, 678.0F, 0.0F, 0.0F, 0.0F, 0.0F}, Idiom::Touch},
    {"duo-inner", {669.0F, 951.0F, 0.0F, 0.0F, 0.0F, 0.0F}, Idiom::Touch},
    {"pad-portrait", {1024.0F, 1366.0F, 24.0F, 20.0F, 0.0F, 0.0F}, Idiom::Touch},
    {"pad-split", {507.0F, 1024.0F, 24.0F, 20.0F, 0.0F, 0.0F}, Idiom::Touch},
    {"pad-landscape", {1366.0F, 1024.0F, 24.0F, 20.0F, 0.0F, 0.0F}, Idiom::Touch},
    {"pad-landscape-11", {1194.0F, 834.0F, 24.0F, 20.0F, 0.0F, 0.0F}, Idiom::Touch},
}};

}  // namespace

Layout desktopLayout() noexcept {
  Layout layout{};
  layout.composition = Composition::Classic;
  layout.idiom = Idiom::Desktop;
  layout.canvasWidth = kClassicWidth;
  layout.canvasHeight = kClassicHeight;
  layout.pointsPerUnit = 1.0F;
  layout.safe = {0.0F, 0.0F, kClassicWidth, kClassicHeight};
  layout.playfield = {0.0F, 0.0F, kPlayfieldWidth, kPlayfieldHeight};
  layout.hud = {panel::kLeft, 0.0F, panel::kWidth, panel::kHeight};
  layout.deck = kNone;
  layout.dialog = kClassicDialog;
  layout.pauseButton = kNone;
  layout.nudgeLeftButton = kNone;
  layout.centreButton = kNone;
  layout.nudgeRightButton = kNone;
  layout.flipperLeftZone = kNone;
  layout.flipperRightZone = kNone;
  return layout;
}

Layout computeLayout(const Surface& surface, Idiom idiom) noexcept {
  if (idiom == Idiom::Desktop) {
    return desktopLayout();
  }
  const Surface s = sanitised(surface);
  const float safeWidth = s.width - s.safeLeft - s.safeRight;
  const float safeHeight = s.height - s.safeTop - s.safeBottom;
  // Units per point each composition needs to fit the safe area. The smaller
  // figure draws the playfield larger.
  const float classicUnits = std::max(kClassicWidth / safeWidth, kClassicHeight / safeHeight);
  const float stackUnits = std::max(kStackWidth / safeWidth, kStackMinHeight / safeHeight);
  const bool useStack = stackUnits <= classicUnits;
  const float units = useStack ? stackUnits : classicUnits;

  Layout layout{};
  layout.idiom = Idiom::Touch;
  layout.canvasWidth = s.width * units;
  layout.canvasHeight = s.height * units;
  layout.pointsPerUnit = 1.0F / units;
  layout.safe = {s.safeLeft * units, s.safeTop * units, safeWidth * units, safeHeight * units};
  return useStack ? stack(layout) : classicTouch(layout);
}

std::span<const Preset> presets() noexcept { return kPresets; }

const Preset* findPreset(std::string_view name) noexcept {
  const auto found = std::ranges::find(kPresets, name, &Preset::name);
  return found == kPresets.end() ? nullptr : &*found;
}

}  // namespace pinball::screen
