#include "platform/PauseMenu.hpp"

#include <string_view>

#include "core/OverlayLayout.hpp"
#include "core/Version.hpp"
#include "platform/Text.hpp"
#include "platform/TouchOverlay.hpp"

namespace pinball {

namespace {

constexpr SDL_Color kAmber{255, 225, 70, 255};
constexpr SDL_Color kCyan{70, 235, 255, 255};
constexpr SDL_Color kGreen{90, 255, 180, 255};
constexpr SDL_Color kPink{255, 60, 170, 255};
constexpr SDL_Color kViolet{150, 90, 255, 255};
constexpr SDL_Color kDim{176, 150, 226, 255};

// The word over the table, and under it how to go on.
void drawPausedWord(SDL_Renderer* r, const screen::Layout& layout, std::string_view hint) {
  // Centred over the playfield.
  const float middle = layout.playfield.x + layout.playfield.w * 0.5F;
  const float top = layout.playfield.y;
  drawText(r, "PAUSED", middle, top + 430.0F, 9.0F, SDL_Color{235, 240, 255, 255},
           TextAlign::Center);
  drawText(r, hint, middle, top + 520.0F, 3.0F, SDL_Color{255, 190, 70, 255}, TextAlign::Center);
}

}  // namespace

void drawPauseMenu(SDL_Renderer* r, const screen::Layout& layout, const MenuState& state) {
  if (r == nullptr) {
    return;
  }
  if (layout.idiom == screen::Idiom::Desktop) {
    drawPausedWord(r, layout,
                   state.afterInterruption ? "CLICK THE WINDOW TO RESUME" : "PRESS F3 TO RESUME");
    return;
  }
  // The table is dimmed by whoever draws it paused; the rest is dimmed here.
  dimAround(r, layout);
  if (state.afterInterruption) {
    // Back from an interruption the hands are not in place yet: no menu,
    // and any tap goes on.
    drawPausedWord(r, layout, "TAP TO RESUME");
    return;
  }
  const screen::MenuLayout menu = screen::menuLayout(layout);
  drawNeonFrame(r, {menu.box.x, menu.box.y, menu.box.w, menu.box.h});
  const float middle = menu.box.x + menu.box.w * 0.5F;
  drawNeonText(r, "PAUSED", middle, menu.box.y + 30.0F, 6.0F, kAmber, kPink, {120, 30, 200, 255},
               TextAlign::Center);
  drawText(r, kGameTitle, middle, menu.box.y + 88.0F, 2.0F, kDim, TextAlign::Center);
  drawCaptionButton(r, menu.resume, "RESUME", kGreen, false);
  if (state.confirmNewGame) {
    drawCaptionButton(r, menu.newGame, "TAP AGAIN TO CONFIRM", kAmber, true);
  } else {
    drawCaptionButton(r, menu.newGame, "NEW GAME", kPink, false);
  }
  drawCaptionButton(r, menu.sound, state.muted ? "SOUND OFF" : "SOUND ON",
                    state.muted ? kViolet : kCyan, false);
  drawCaptionButton(r, menu.music, state.musicOn ? "MUSIC ON" : "MUSIC OFF",
                    state.musicOn ? kCyan : kViolet, false);
  drawCaptionButton(r, menu.about, "SCORES & ABOUT", kViolet, false);
}

}  // namespace pinball
