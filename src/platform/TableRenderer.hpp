#pragma once

#include <SDL3/SDL.h>

#include <memory>

#include "core/Game.hpp"
#include "platform/Assets.hpp"

namespace pinball {

// Draws the game onto the logical canvas. Owns every texture. Used by the
// game window and by the offscreen snapshot tool, so both show the same thing.
class TableRenderer {
 public:
  // Loads all textures. Returns nullptr, after logging, if any is missing.
  static std::unique_ptr<TableRenderer> create(SDL_Renderer* renderer, const AssetLoader& assets);

  // Draws one frame. Does not clear or present. dimmed darkens the playfield,
  // used while paused.
  void draw(SDL_Renderer* renderer, const Game& game, bool dimmed) const;

 private:
  struct Textures {
    TexturePtr background;
    TexturePtr ball;
    TexturePtr flipper;
    TexturePtr bumper;
    TexturePtr glow;
    TexturePtr bar;
    TexturePtr plunger;
  };
  struct Rgb {
    Uint8 r;
    Uint8 g;
    Uint8 b;
  };

  explicit TableRenderer(Textures textures);

  // A soft additive light of the given colour and strength (0 to 1).
  void drawGlow(SDL_Renderer* renderer, Vec2 center, float radius, Rgb color,
                float strength) const;
  void drawBumpers(SDL_Renderer* renderer, const Game& game) const;
  void drawTargets(SDL_Renderer* renderer, const Game& game) const;
  void drawPlungerAndGate(SDL_Renderer* renderer, const Game& game) const;
  void drawFlippers(SDL_Renderer* renderer, const Game& game) const;
  void drawBall(SDL_Renderer* renderer, const Game& game) const;

  Textures textures_;
};

}  // namespace pinball
