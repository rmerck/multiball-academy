#pragma once

#include <array>

#include "core/ScreenLayout.hpp"

// Rectangles of the overlays (pause menu, About, initials), worked out from
// Layout::dialog so that drawing and hit-testing share them.
namespace pinball::screen {

struct MenuLayout {
  Rect box;
  Rect resume;
  Rect newGame;
  Rect sound;
  Rect music;
  Rect about;
};

struct AboutLayout {
  Rect box;
  Rect close;
  std::array<Rect, 3> tabs;
  Rect body;
  Rect privacyLink;
  Rect previous;
  Rect next;
};

struct InitialsLayout {
  Rect box;
  // One of each per letter. up and down, and ok, are empty for Desktop.
  std::array<Rect, 3> up;
  std::array<Rect, 3> slot;
  std::array<Rect, 3> down;
  Rect ok;
};

[[nodiscard]] MenuLayout menuLayout(const Layout& layout) noexcept;
[[nodiscard]] AboutLayout aboutLayout(const Layout& layout) noexcept;
[[nodiscard]] InitialsLayout initialsLayout(const Layout& layout) noexcept;

// True if the point is in the rectangle. The left and top edges are in, the
// right and bottom ones are not, so adjoining rectangles never both match.
// An empty rectangle contains nothing.
[[nodiscard]] constexpr bool contains(const Rect& r, float x, float y) noexcept {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

}  // namespace pinball::screen
