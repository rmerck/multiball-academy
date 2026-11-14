#pragma once

#include <array>
#include <cstddef>

#include "core/FrontMenu.hpp"
#include "core/ScreenLayout.hpp"

// Rectangles of the front screens (title, high scores, how to play,
// settings) and of the touch guide, worked out from the composition so that
// drawing and hit-testing share them. Every control is at least
// screen::kMinHitUnits on a side.
namespace pinball::screen {

// The title's menu buttons: this wide, and as tall as kMinHitPoints needs
// on the screen but not under this many units.
inline constexpr float kTitleItemWidth = 440.0F;
inline constexpr float kTitleItemMinUnits = 64.0F;

struct TitleLayout {
  // The whole composition: the table, its status display and its deck.
  Rect area;
  Rect logo;
  // The dot display under the logo.
  Rect marquee;
  // One per TitleItem; an item that is not shown has an empty rectangle.
  std::array<Rect, kTitleItemCount> items;
  // One line of text under the menu.
  Rect footer;
};

struct PageLayout {
  Rect box;
  Rect heading;
  Rect body;
  Rect back;
  // How to play only; empty elsewhere.
  Rect previous;
  Rect next;
  Rect guide;
};

struct SettingsLayout {
  PageLayout page;
  // One per SettingsRow; Volume's is the whole row, Back is page.back, a
  // row that is not shown is empty.
  std::array<Rect, kSettingsRowCount> rows;
  Rect volumeDown;
  Rect volumeUp;
};

// The touch guide: a label beside each control, and the card in the middle
// that says how to go on. No label lies on a control, on another label or
// on the card; the pause button's lies on none of the display's windows
// either.
struct GuideLayout {
  Rect card;
  Rect leftLabel;
  Rect rightLabel;
  Rect centreLabel;
  Rect nudgeLeftLabel;
  Rect nudgeRightLabel;
  Rect pauseLabel;
  // A line from the pause button to its label where the label cannot sit
  // right beside it (the Stack's strip is all windows); else empty.
  Rect pauseLead;
};

// Where attract mode's table goes behind the front screens: the whole table,
// as large as the safe area holds it (its own shape, 4:5), centred on it.
// The status display and the controls are not drawn there.
[[nodiscard]] Rect attractArea(const Layout& layout) noexcept;

// The composition's whole area: Stack's column or Classic's 1280 x 960.
[[nodiscard]] Rect compositionArea(const Layout& layout) noexcept;
[[nodiscard]] TitleLayout titleLayout(const Layout& layout, const FrontConfig& config) noexcept;
// For a page of the front screens other than the title. guideButton: how to
// play shows the button that replays the touch guide.
[[nodiscard]] PageLayout pageLayout(const Layout& layout, FrontPage page,
                                    bool guideButton = false) noexcept;
[[nodiscard]] SettingsLayout settingsLayout(const Layout& layout,
                                            const FrontConfig& config) noexcept;
[[nodiscard]] GuideLayout guideLayout(const Layout& layout) noexcept;

// What lies under a point of the front screen that state shows.
[[nodiscard]] FrontTarget frontTarget(const Layout& layout, const FrontConfig& config,
                                      const FrontState& state, float x, float y) noexcept;

}  // namespace pinball::screen
