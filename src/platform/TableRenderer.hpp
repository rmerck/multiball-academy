#pragma once

#include <SDL3/SDL.h>

#include <memory>

#include "core/Game.hpp"
#include "platform/Assets.hpp"
#include "platform/Hud.hpp"

namespace pinball {

// Draws the game onto the logical canvas. Owns every texture. Used by the
// game window and by the offscreen snapshot tool, so both show the same thing.
class TableRenderer {
 public:
  // Loads all textures. Returns nullptr, after logging, if any is missing.
  [[nodiscard]] static std::unique_ptr<TableRenderer> create(SDL_Renderer* renderer,
                                                             const AssetLoader& assets);

  // Draws one frame. Does not clear or present. alpha is the fraction of a
  // step since the latest one (FixedTimestep::alpha()): the ball and the
  // flippers are drawn that far between the previous step and the latest, so
  // motion is smooth when the display does not refresh in step with the
  // simulation. 1 draws the latest step exactly. Everything else shows the
  // latest step. dimmed darkens the playfield, used while paused.
  void draw(SDL_Renderer* renderer, const Game& game, float alpha, bool dimmed,
            const HudExtras& extras = {}) const;

 private:
  struct Textures {
    TexturePtr background;
    TexturePtr ball;
    TexturePtr flipper;
    TexturePtr bumper;
    TexturePtr glow;
    TexturePtr bar;
    TexturePtr plunger;
    TexturePtr panel;
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
  void drawLights(SDL_Renderer* renderer, const Game& game) const;
  void drawBumpers(SDL_Renderer* renderer, const Game& game) const;
  void drawTargets(SDL_Renderer* renderer, const Game& game) const;
  void drawPlungerAndGate(SDL_Renderer* renderer, const Game& game) const;
  void drawFlippers(SDL_Renderer* renderer, const Game& game, float alpha) const;
  void drawBall(SDL_Renderer* renderer, const Game& game, float alpha) const;

  Textures textures_;
};

}  // namespace pinball
