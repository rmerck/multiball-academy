#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <vector>

#include "core/Game.hpp"
#include "platform/Assets.hpp"

namespace pinball {

// Draws the game onto the logical canvas. Owns every texture. Used by the
// game window and by the offscreen snapshot tool, so both show the same thing.
class TableRenderer {
 public:
  // Loads all textures. Returns nullptr, after logging, if any is missing.
  static std::unique_ptr<TableRenderer> create(SDL_Renderer* renderer, const AssetLoader& assets);

  // Draws one frame. Does not clear or present. dimmed draws the ball
  // translucent, used while paused.
  void draw(SDL_Renderer* renderer, const Game& game, bool dimmed) const;

 private:
  struct Textures {
    TexturePtr ball;
  };

  explicit TableRenderer(Textures textures);

  void drawOutline(SDL_Renderer* renderer, const Game& game) const;

  Textures textures_;
  std::vector<SDL_FPoint> outline_;
};

}  // namespace pinball
