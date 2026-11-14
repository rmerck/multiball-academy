#include "platform/PauseMenu.hpp"

#include <string_view>

#include "core/OverlayLayout.hpp"
#include "core/Version.hpp"
#include "generated/ArtPalette.hpp"
#include "platform/Text.hpp"
#include "platform/TouchOverlay.hpp"

namespace pinball {

namespace {

// The art's palette (tools/art/palette.py).
constexpr SDL_Color sdl(art::Rgb c) noexcept { return {c.r, c.g, c.b, 255}; }
constexpr SDL_Color kYellow = sdl(art::kYellow);
constexpr SDL_Color kOrange = sdl(art::kOrange);
constexpr SDL_Color kTeal = sdl(art::kTeal);
constexpr SDL_Color kPink = sdl(art::kPink);
constexpr SDL_Color kViolet = sdl(art::kViolet);
constexpr SDL_Color kOrchid = sdl(art::kOrchid);
constexpr SDL_Color kCream = sdl(art::kCream);

// The word over the table, and under it how to go on.
void drawPausedWord(SDL_Renderer* r, const screen::Layout& layout, std::string_view hint) {
  // Centred over the playfield.
  const float middle = layout.playfield.x + layout.playfield.w * 0.5F;
  const float top = layout.playfield.y;
  drawText(r, "PAUSED", middle, top + 430.0F, 9.0F, kCream, TextAlign::Center);
  drawText(r, hint, middle, top + 520.0F, 3.0F, kYellow, TextAlign::Center);
}

}  // namespace

void drawPauseMenu(SDL_Renderer* r, const screen::Layout& layout, const MenuState& state,
                   const OverlayArt& art) {
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
  drawDialogCard(r, art, {menu.box.x, menu.box.y, menu.box.w, menu.box.h});
  const float middle = menu.box.x + menu.box.w * 0.5F;
  // Above the buttons: the title lockup, or the word and the game's name.
  const screen::Rect header{menu.box.x + 32.0F, menu.box.y + 16.0F, menu.box.w - 64.0F,
                            menu.resume.y - menu.box.y - 28.0F};
  if (!drawTitleArt(r, art.title, header)) {
    drawNeonText(r, "PAUSED", middle, menu.box.y + 30.0F, 6.0F, kYellow, kPink, kViolet,
                 TextAlign::Center);
    drawText(r, kGameTitle, middle, menu.box.y + 84.0F, screen::smallestTextSize(layout),
             kOrchid, TextAlign::Center);
  }
  const ButtonArt& button = art.button;
  drawCaptionButton(r, menu.resume, "RESUME", kTeal, false, 4.0F, 3.0F, button);
  if (state.confirmNewGame) {
    drawCaptionButton(r, menu.newGame, "TAP AGAIN TO CONFIRM", kOrange, true, 4.0F, 3.0F,
                      button);
  } else {
    drawCaptionButton(r, menu.newGame, "NEW GAME", kButtonOrchid, false, 4.0F, 3.0F, button);
  }
  // A setting that is on is a coloured key, one that is off a grey one.
  drawCaptionButton(r, menu.sound, state.muted ? "SOUND OFF" : "SOUND ON",
                    state.muted ? kButtonOff : kButtonOrchid, false, 4.0F, 3.0F, button);
  drawCaptionButton(r, menu.music, state.musicOn ? "MUSIC ON" : "MUSIC OFF",
                    state.musicOn ? kButtonOrchid : kButtonOff, false, 4.0F, 3.0F, button);
  drawCaptionButton(r, menu.about, "SCORES & ABOUT", kPink, false, 4.0F, 3.0F, button);
}

}  // namespace pinball
