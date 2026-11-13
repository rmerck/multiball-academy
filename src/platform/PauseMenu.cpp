#include "platform/PauseMenu.hpp"

#include "platform/Text.hpp"

namespace pinball {

void drawPauseMenu(SDL_Renderer* r, const screen::Layout& layout, const MenuState& state) {
  if (r == nullptr) {
    return;
  }
  // Centred over the playfield.
  const float middle = layout.playfield.x + layout.playfield.w * 0.5F;
  const float top = layout.playfield.y;
  drawText(r, "PAUSED", middle, top + 430.0F, 9.0F, SDL_Color{235, 240, 255, 255},
           TextAlign::Center);
  drawText(r, state.afterInterruption ? "CLICK THE WINDOW TO RESUME" : "PRESS F3 TO RESUME", middle,
           top + 520.0F, 3.0F, SDL_Color{255, 190, 70, 255}, TextAlign::Center);
}

}  // namespace pinball
