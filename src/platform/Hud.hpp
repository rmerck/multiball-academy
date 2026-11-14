#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <optional>
#include <string_view>

#include "core/Game.hpp"
#include "core/ScreenLayout.hpp"
#include "platform/GameViews.hpp"

namespace pinball {

// For the review tool: things to show that a scripted game cannot be steered
// into. The game itself never sets one.
struct HudPreview {
  // Shown in place of the rules' message, if not empty.
  std::string_view message;
  // Show a running course with the longest name and goal there are, a
  // three-figure count: the fullest the course window gets.
  bool course = false;
  // Shown in place of what the rules say about multiball.
  std::optional<MultiballView> multiball;
};

// Things the panel shows that are not part of the game's own state.
struct HudExtras {
  std::uint64_t bestScore = 0;
  // The game that just ended made the high score table.
  bool newHighScore = false;
  const HudPreview* preview = nullptr;
};

// Art for the status display. Any of it may be null: the display then draws
// plain windows and lettering of its own.
struct HudArt {
  SDL_Texture* panel = nullptr;  // Classic, 512 x 960 units.
  SDL_Texture* strip = nullptr;  // Stack, 768 x 232 units.
  SDL_Texture* title = nullptr;  // The game's name, 480 x 120 units.
};

// Draws the status display at layout.hud: score, ball, rank, course, awards
// and messages. Classic gets the panel beside the table, Stack the strip
// above it. The keyboard hints, and the key that starts a new game, are
// shown to the desktop idiom only; a touch screen has its buttons there
// (TableRenderer::drawTouchOverlay).
void drawHud(SDL_Renderer* renderer, const HudArt& art, const Game& game,
             const screen::Layout& layout, const HudExtras& extras);

}  // namespace pinball
