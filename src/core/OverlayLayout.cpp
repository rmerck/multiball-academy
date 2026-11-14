#include "core/OverlayLayout.hpp"

#include <algorithm>
#include <cstddef>

// The boxes are where the desktop's dialogs have always been drawn. The
// controls inside them are what the touch screens draw and hit-test; every
// one is at least kMinHitUnits on a side.
namespace pinball::screen {

namespace {

constexpr float kButtonHeight = kMinHitUnits;
constexpr float kGap = 16.0F;
constexpr float kMargin = 32.0F;

constexpr Rect centredIn(const Rect& outer, float w, float h) noexcept {
  return {outer.x + (outer.w - w) * 0.5F, outer.y + (outer.h - h) * 0.5F, w, h};
}

// The initials prompt, as drawn by platform/InitialsScreen.cpp.
constexpr float kInitialsWidth = 700.0F;
constexpr float kInitialsHeight = 460.0F;
constexpr float kInitialsTouchHeight = 716.0F;
constexpr float kLetterPitch = 120.0F;
constexpr float kLetterHeight = 98.0F;
constexpr float kLettersTop = 196.0F;

}  // namespace

MenuLayout menuLayout(const Layout& layout) noexcept {
  constexpr float kTitleHeight = 120.0F;
  constexpr float kRows = 5.0F;
  const float width = std::min(layout.dialog.w, 560.0F);
  const float height = kTitleHeight + kRows * kButtonHeight + (kRows - 1.0F) * kGap + kMargin;
  MenuLayout menu{};
  menu.box = centredIn(layout.dialog, width, height);
  const auto row = [&menu](float index) noexcept {
    return Rect{menu.box.x + kMargin, menu.box.y + kTitleHeight + index * (kButtonHeight + kGap),
                menu.box.w - 2.0F * kMargin, kButtonHeight};
  };
  menu.resume = row(0.0F);
  menu.newGame = row(1.0F);
  // Sound and music share a row, half each.
  const Rect both = row(2.0F);
  const float half = (both.w - kGap) * 0.5F;
  menu.sound = {both.x, both.y, half, both.h};
  menu.music = {both.x + half + kGap, both.y, both.w - half - kGap, both.h};
  menu.about = row(3.0F);
  menu.mainMenu = row(4.0F);
  return menu;
}

AboutLayout aboutLayout(const Layout& layout) noexcept {
  AboutLayout about{};
  about.box = layout.dialog;
  const Rect& box = about.box;
  const float inner = box.w - 2.0F * kMargin;
  const float tabWidth = (inner - 2.0F * kGap) / 3.0F;
  for (std::size_t i = 0; i < about.tabs.size(); ++i) {
    about.tabs[i] = {box.x + kMargin + static_cast<float>(i) * (tabWidth + kGap), box.y + kMargin,
                     tabWidth, kButtonHeight};
  }
  // Along the bottom: previous page, close, next page.
  const float bottom = box.y + box.h - kMargin - kButtonHeight;
  about.previous = {box.x + kMargin, bottom, kButtonHeight, kButtonHeight};
  about.next = {box.x + box.w - kMargin - kButtonHeight, bottom, kButtonHeight, kButtonHeight};
  about.close = {about.previous.x + kButtonHeight + kGap, bottom,
                 inner - 2.0F * (kButtonHeight + kGap), kButtonHeight};
  // The privacy button sits above that row, the text between it and the tabs.
  about.privacyLink = {box.x + kMargin, bottom - kGap - kButtonHeight, inner, kButtonHeight};
  const float bodyTop = box.y + kMargin + kButtonHeight + kGap;
  about.body = {box.x + kMargin, bodyTop, inner, about.privacyLink.y - kGap - bodyTop};
  return about;
}

InitialsLayout initialsLayout(const Layout& layout) noexcept {
  const bool touch = layout.idiom == Idiom::Touch;
  InitialsLayout initials{};
  initials.box =
      centredIn(layout.dialog, kInitialsWidth, touch ? kInitialsTouchHeight : kInitialsHeight);
  const Rect& box = initials.box;
  const float middle = box.x + box.w * 0.5F;
  // On a touch screen an arrow above and below each letter makes room.
  const float slotTop = box.y + kLettersTop + (touch ? kButtonHeight + kGap : 0.0F);
  for (std::size_t i = 0; i < initials.slot.size(); ++i) {
    const float left = middle + (static_cast<float>(i) - 1.5F) * kLetterPitch;
    initials.slot[i] = {left, slotTop, kLetterPitch, kLetterHeight + 2.0F * kGap};
    if (touch) {
      initials.up[i] = {left, slotTop - kGap - kButtonHeight, kLetterPitch, kButtonHeight};
      initials.down[i] = {left, slotTop + initials.slot[i].h + kGap, kLetterPitch, kButtonHeight};
    }
  }
  if (touch) {
    initials.ok = {middle - 140.0F, box.y + box.h - kMargin - kButtonHeight, 280.0F, kButtonHeight};
  }
  return initials;
}

}  // namespace pinball::screen
