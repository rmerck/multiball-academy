#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <span>

#include "core/Game.hpp"
#include "core/ScreenLayout.hpp"
#include "core/TouchControls.hpp"
#include "platform/Assets.hpp"
#include "platform/Hud.hpp"

namespace pinball {

struct DrawOptions {
  // Darkens the playfield, used while paused.
  bool dimmed = false;
  // The system asks for less motion: the table does not jolt on a nudge.
  bool reduceMotion = false;
};

// Which textures to load. Desktop: what the Classic composition draws with.
// All: the art of the touch layouts as well.
enum class TextureSet : std::uint8_t { Desktop, All };

// Draws the game onto the logical canvas. Owns every texture. Used by the
// game window and by the offscreen snapshot tool, so both show the same thing.
class TableRenderer {
 public:
  // Loads the textures. Returns nullptr, after logging, if any is missing.
  // (There is no touch art yet, so both sets are the same.)
  [[nodiscard]] static std::unique_ptr<TableRenderer> create(SDL_Renderer* renderer,
                                                             const AssetLoader& assets,
                                                             TextureSet set);

  // Fills what the composition leaves of the canvas: the gutters, the bands
  // outside the safe area, the margin round Classic on a touch screen. Call
  // after clearing and before draw. The desktop's composition is the whole
  // canvas, so there it draws nothing.
  void drawBackdrop(SDL_Renderer* renderer, const screen::Layout& layout) const;

  // Draws one frame: the table at layout.playfield and the status display at
  // layout.hud. Does not clear or present. alpha is the fraction of a step
  // since the latest one (FixedTimestep::alpha()): the ball and the flippers
  // are drawn that far between the previous step and the latest, so motion
  // is smooth when the display does not refresh in step with the
  // simulation. 1 draws the latest step exactly. Everything else shows the
  // latest step.
  void draw(SDL_Renderer* renderer, const Game& game, float alpha, const screen::Layout& layout,
            const DrawOptions& options, const HudExtras& extras = {}) const;

  // Draws the touch controls over the frame, held ones pressed in. Not
  // written yet: it draws nothing.
  void drawTouchOverlay(SDL_Renderer* renderer, const screen::Layout& layout,
                        const TouchControls::View& view) const;

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
  // Every ball that is in sight, those on the table first and those riding
  // the ramp over them.
  void drawBalls(SDL_Renderer* renderer, std::span<const BallView> balls) const;

  Textures textures_;
};

}  // namespace pinball
