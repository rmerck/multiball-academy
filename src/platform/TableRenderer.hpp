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
  // For the review tool: balls drawn as well as the game's own, so a table
  // with several can be looked at before the game can be played into one.
  std::span<const BallView> extraBalls{};
};

// Which textures to load. Desktop: what the Classic composition draws with.
// All: the art of the touch layouts as well (ui/strip.png, ui/deck.png,
// ui/backdrop.png, sprites/button.png) and the title (ui/title.png). Each of
// those is used if the build's asset manifest lists it; where it does not,
// plain windows, buttons and lettering are drawn in its place, so the art
// can arrive file by file.
enum class TextureSet : std::uint8_t { Desktop, All };

// Draws the game onto the logical canvas. Owns every texture.
//
// The playfield itself is art (table/background.png) when the build has it.
// When the asset manifest does not list that file, the table is drawn from
// src/core/TableLayout.hpp instead, in plain colours: every wall, lane,
// element outline and lamp where the physics has it, once, into a texture
// that each frame then copies. That is what a build
// shows while the art for a new layout is still to be drawn, so that the
// picture always matches the table being played. Used by the
// game window and by the offscreen snapshot tool, so both show the same thing.
class TableRenderer {
 public:
  // Loads the textures. Returns nullptr, after logging, if one the manifest
  // lists cannot be loaded.
  [[nodiscard]] static std::unique_ptr<TableRenderer> create(SDL_Renderer* renderer,
                                                             const AssetLoader& assets,
                                                             TextureSet set);

  // Fills what the composition leaves of the canvas: the gutters, the bands
  // outside the safe area, the margin round Classic on a touch screen. Call
  // after clearing and before draw. The desktop's composition is the whole
  // canvas, so there it draws nothing. Without backdrop art it is the flat
  // screen::kBackdropRgb under a lattice of dim dots.
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

  // Draws the touch controls over the frame, held ones pressed in: the
  // Stack's deck with its button row and flipper pads, the Classic panel's
  // row, and the pause button. Call after draw and before any dialog. Draws
  // nothing for the desktop idiom.
  void drawTouchOverlay(SDL_Renderer* renderer, const screen::Layout& layout,
                        const TouchControls::View& view) const;

 private:
  struct Textures {
    TexturePtr background;  // The art, or the table drawn from its layout.
    TexturePtr ball;
    TexturePtr flipper;
    TexturePtr bumper;
    TexturePtr glow;
    TexturePtr bar;
    TexturePtr plunger;
    TexturePtr panel;
    // The rest may be null: see TextureSet.
    TexturePtr strip;
    TexturePtr deck;
    TexturePtr backdrop;
    TexturePtr title;
    TexturePtr button;
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
  void drawTargets(SDL_Renderer* renderer) const;
  void drawSlingshotFlashes(SDL_Renderer* renderer, const Game& game) const;
  void drawPlunger(SDL_Renderer* renderer, const Game& game) const;
  void drawFlippers(SDL_Renderer* renderer, const Game& game, float alpha) const;
  // Every ball that is in sight, those on the table first and those riding
  // a ramp's rail over them.
  void drawBalls(SDL_Renderer* renderer, std::span<const BallView> balls) const;

  Textures textures_;
};

}  // namespace pinball
