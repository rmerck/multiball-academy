#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/FrontMenu.hpp"
#include "core/HighScores.hpp"
#include "core/ScreenLayout.hpp"
#include "core/Settings.hpp"
#include "platform/TouchOverlay.hpp"

// The screens in front of the game: the title over attract mode, with its
// menu; the high scores; how to play; the settings; and the touch guide.
// Drawn from core/FrontLayout.hpp, which the touch mapper and the layout
// checks share. Over a frame the caller has drawn: the attract table,
// dimmed, for the front screens; the game itself for the touch guide.
namespace pinball {

// How long the title takes to build itself: the logo scans in, flashes,
// the dot display lights and the menu comes up. Any key or tap skips it.
inline constexpr double kTitleIntroSeconds = 1.6;
// How strongly attract mode's table is veiled behind the title (enough for
// the menu to stand clear, little enough that the machine reads as lit),
// and behind the other pages, which are on cards of their own.
inline constexpr std::uint8_t kTitleVeil = 70;
inline constexpr std::uint8_t kPageVeil = 120;

struct FrontView {
  FrontState state;
  FrontConfig config;
  // Since the title came up. At kTitleIntroSeconds or later it is built.
  double introSeconds = kTitleIntroSeconds;
  // A clock for what keeps moving once it is: the dot display and the
  // light that runs across the logo now and then.
  double seconds = 0.0;
  // Nothing moves for show: the title is built at once, the dot display
  // changes page without sliding, no light runs across the logo.
  bool reduceMotion = false;
  // The keyboard is the way in (a desktop): the focus is drawn, and the
  // words name keys.
  bool keyboard = false;
  const HighScores* scores = nullptr;
  const Settings* settings = nullptr;
  // What a finger is holding down, drawn pressed.
  FrontTarget pressed{};
};

void drawFront(SDL_Renderer* renderer, const screen::Layout& layout, const FrontView& view,
               const OverlayArt& art);

// The touch guide over the game: each control's area marked and named, and
// a card that says how to go on. firstGame: before the first game (tap to
// play), else from HOW TO PLAY (tap to go back). Touch layouts only.
void drawTouchGuide(SDL_Renderer* renderer, const screen::Layout& layout, bool firstGame,
                    const OverlayArt& art);

// What the title's dot display shows at a time: the messages in turn
// (press play, the name, the high scores), each sliding in and holding,
// or a longer one running across. Exposed for the layout check.
[[nodiscard]] std::size_t marqueeMessageCount(const HighScores* scores) noexcept;

// The words on each control and title of a front screen, for VoiceOver.
[[nodiscard]] std::string_view titleItemLabel(TitleItem item) noexcept;

}  // namespace pinball
