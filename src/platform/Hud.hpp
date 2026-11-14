#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <string_view>

#include "core/Game.hpp"
#include "core/ScreenLayout.hpp"
#include "generated/ArtPalette.hpp"
#include "platform/GameViews.hpp"

namespace pinball {

// For the review tool: things to show that a scripted game cannot be steered
// into. The game itself never sets one.
struct HudPreview {
  // Shown in place of the rules' message, if not empty.
  std::string_view message;
  // Show a running course with the longest name and goal there are: the
  // fullest the course window gets.
  bool course = false;
};

// Things the panel shows that are not part of the game's own state.
struct HudExtras {
  std::uint64_t bestScore = 0;
  // The game that just ended made the high score table.
  bool newHighScore = false;
  const HudPreview* preview = nullptr;
};

// The colours the status display letters in, from the art's palette: the
// first five for text, the last two for its lamp boxes. The display's art
// has none of them exactly inside a window, so the layout check can tell
// lettering from art by them.
inline constexpr std::array<SDL_Color, 7> kHudLettering{{
    {art::kYellow.r, art::kYellow.g, art::kYellow.b, 255},  // Digits, messages.
    {art::kAqua.r, art::kAqua.g, art::kAqua.b, 255},        // Rank.
    {art::kCream.r, art::kCream.g, art::kCream.b, 255},     // Text.
    {art::kOrchid.r, art::kOrchid.g, art::kOrchid.b, 255},  // Labels.
    {art::kGreen.r, art::kGreen.g, art::kGreen.b, 255},     // Ball save.
    {art::kPink.r, art::kPink.g, art::kPink.b, 255},        // Game over, tilt.
    {96, 88, 170, 255},                                     // An unlit lamp's word.
}};

// Art for the status display. Any of it may be null: the display then draws
// plain windows and lettering of its own.
struct HudArt {
  SDL_Texture* panel = nullptr;  // Classic, 512 x 960 units.
  SDL_Texture* strip = nullptr;  // Stack, 768 x 232 units.
  SDL_Texture* title = nullptr;  // The game's name, 480 x 120 units.
};

// Draws the status display at layout.hud: score and best score; ball, extra
// balls, multiplier, combo, ball saver, W-I-N-G and kickback; rank; the
// course with its clock, goal and progress, and the course lamps; bay doors,
// lock bays, turbo meter and jackpot; messages. Every lamp-like thing shows
// what the table's own lamp shows (TableLights.hpp). Classic gets the panel beside the table, Stack the strip
// above it. The keyboard hints, and the key that starts a new game, are
// shown to the desktop idiom only; a touch screen has its buttons there
// (TableRenderer::drawTouchOverlay).
void drawHud(SDL_Renderer* renderer, const HudArt& art, const Game& game,
             const screen::Layout& layout, const HudExtras& extras);

}  // namespace pinball
