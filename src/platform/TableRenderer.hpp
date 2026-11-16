#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/Game.hpp"
#include "core/ScreenLayout.hpp"
#include "core/TouchControls.hpp"
#include "generated/ArtPalette.hpp"
#include "platform/Assets.hpp"
#include "platform/Hud.hpp"
#include "platform/TableEffects.hpp"
#include "platform/TableLights.hpp"
#include "platform/TouchOverlay.hpp"

namespace pinball {

struct DrawOptions {
  // Darkens the playfield, used while paused.
  bool dimmed = false;
  // The system asks for less motion: the table does not jolt on a nudge.
  bool reduceMotion = false;
  // The flippers drawn down whatever the physics holds: a paused game has
  // let go of every button, and a bat held up when it stopped drops as soon
  // as it goes on, so the picture under the pause shows it down.
  bool flippersAtRest = false;
  // For the review tool: balls drawn as well as the game's own, so a table
  // with several can be looked at before the game can be played into one.
  std::span<const BallView> extraBalls{};
  // The light show to draw (core/LightShow.hpp), fed every step by the
  // caller. Null: the lamps alone, lit or not, as the rules say.
  const LightShow* lightShow = nullptr;
};

// Which textures to load. Desktop: what the Classic composition draws with,
// the title (ui/title.png) and the dialog card (ui/dialog.png) included.
// All: the art of the touch layouts as well (ui/strip.png, ui/deck.png,
// ui/backdrop.png, sprites/button.png, sprites/button_gloss.png). Each of
// those is used if the build's asset manifest lists it; where it does not,
// plain windows, buttons and lettering are drawn in its place.
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

  // Attract mode, behind the title: the table alone (no status display, no
  // controls), scaled to fill area, lit as lights says, under a veil of the
  // backdrop colour of the given strength (0 to 255).
  void drawAttract(SDL_Renderer* renderer, const Game& game, float alpha, const screen::Rect& area,
                   const TableLights& lights, std::uint8_t veil, bool reduceMotion,
                   const LightShow* show = nullptr) const;

  // Draws the touch controls over the frame, held ones pressed in: the
  // Stack's deck with its button row and flipper pads, the Classic panel's
  // row, and the pause button. Call after draw and before any dialog. Draws
  // nothing for the desktop idiom.
  void drawTouchOverlay(SDL_Renderer* renderer, const screen::Layout& layout,
                        const TouchControls::View& view) const;

  // Economy: the light show without the general illumination's light map,
  // its one whole-playfield layer. On by default for the software renderer,
  // which blends every pixel of it on the processor; the review tools turn
  // it off to see everything.
  void setEffectsEconomy(bool on) noexcept { economy_ = on; }
  [[nodiscard]] bool effectsEconomy() const noexcept { return economy_; }

  // The art the dialogs (pause, About, initials) draw with. Valid for as
  // long as this renderer.
  [[nodiscard]] OverlayArt overlayArt() const noexcept;

 private:
  struct Textures {
    TexturePtr background;  // The art, or the table drawn from its layout.
    // The background is the layout drawing, so the table's moving parts are
    // drawn the same plain way over it.
    bool backgroundFromLayout = false;
    TexturePtr ball;
    TexturePtr flipper;
    TexturePtr bumper;
    TexturePtr bumperCap;
    TexturePtr glow;
    TexturePtr flare;  // The light show's flasher bursts.
    TexturePtr ring;   // And its shock rings.
    TexturePtr gi;     // And the general illumination's light map.
    TexturePtr pool;   // And the dark pool round each ball.
    TexturePtr flasherDome;  // Revision 3's flasher domes, lit (unlit, they are in the art).
    TexturePtr bar;
    TexturePtr door;
    TexturePtr plunger;
    TexturePtr panel;
    // Over the table art only: the layout drawing has its own.
    TexturePtr insertArrow;
    TexturePtr insertRound;
    TexturePtr insertHex;      // Revision 3's drone armour.
    TexturePtr insertChevron;  // And its rank ladder.
    std::array<TexturePtr, 2> spinner;  // Edge-on, flat.
    TexturePtr kickback;
    // Table A's art (assets/table_a, tools/art/table_a): when the build has
    // table_a/background.png, the table is drawn from it and these sheets
    // (generated/ArtPalette.hpp art::sheet), over the art's baked unlit
    // inserts, walls and plastics.
    bool tableA = false;
    struct TableA {
      TexturePtr drone;
      TexturePtr propwash;
      TexturePtr propwashBlur;
      TexturePtr pylon;
      TexturePtr bayDoor;
      TexturePtr flightDeck;
      TexturePtr rampRunway;
      TexturePtr rampSkyway;
      TexturePtr jetCaps;
      TexturePtr domes;
      TexturePtr arrows;
      TexturePtr flaps;
      TexturePtr rungs;
      TexturePtr plate;
      TexturePtr captiveBall;
    } a;
    // The rest may be null: see TextureSet.
    TexturePtr title;
    TexturePtr dialog;
    TexturePtr dot;
    TexturePtr strip;
    TexturePtr deck;
    TexturePtr backdrop;
    TexturePtr button;
    TexturePtr buttonGloss;
  };

  TableRenderer(Textures textures, std::vector<SDL_Vertex> outside);

  // The table and what moves on it, with its top left corner at left, top
  // on the current target, lit as lights says.
  void drawTable(SDL_Renderer* renderer, const Game& game, float alpha, int left, int top,
                 const DrawOptions& options, const TableLights& lights) const;

  // A soft additive light of the given colour and strength (0 to 1, held
  // to art::kMaxGlowStrength so that no light outshines the ball).
  void drawGlow(SDL_Renderer* renderer, Vec2 center, float radius, art::Rgb color,
                float strength) const;
  // The lit lamps over the art's unlit inserts, each with its glow. With a
  // light show, each at its own level, in neon.
  void drawLamps(SDL_Renderer* renderer, const TableLights& lights, const LightShow* show) const;
  // The insert sprites by InsertKind (arrow, round, hex, chevron).
  [[nodiscard]] std::array<SDL_Texture*, 4> insertTextures() const noexcept {
    return {textures_.insertArrow.get(), textures_.insertRound.get(), textures_.insertHex.get(),
            textures_.insertChevron.get()};
  }
  // Each spinner's blade by its own phase, and the kickback's spring.
  void drawSpinnerAndKickback(SDL_Renderer* renderer, const Game& game) const;
  // Table revision 3's moving parts: the glow of a ball riding the Briefing
  // room's subway under the playfield, and the lit tube it runs in; the
  // training drone, patrolling, recoiling from a hit, sinking and coming up;
  // the lane's one-way gate, its flap lifting as a ball goes up through it.
  void drawSubway(SDL_Renderer* renderer, std::span<const BallView> balls) const;
  void drawDrone(SDL_Renderer* renderer, const Game& game, const LightShow* show) const;
  void drawLaneGate(SDL_Renderer* renderer, std::span<const BallView> balls) const;
  void drawBumpers(SDL_Renderer* renderer, const Game& game, const LightShow* show) const;
  void drawTargets(SDL_Renderer* renderer, const Game& game, const TableLights& lights) const;
  // What moves or lights among the table's own elements, for the layout
  // drawing: doors lying down, the lock and kickback lamps, the spinner.
  void drawElements(SDL_Renderer* renderer, const Game& game, const TableLights& lights) const;
  void drawSlingshotFlashes(SDL_Renderer* renderer, const Game& game, const LightShow* show) const;
  // With a light show: the bumpers lit, flashing white-hot and pulsing
  // when struck; the slingshots' kicking faces flashing.
  void drawLitBumpers(SDL_Renderer* renderer, const Game& game, const LightShow& show) const;
  void drawLitSlingshots(SDL_Renderer* renderer, const Game& game, const LightShow& show) const;
  void drawPlunger(SDL_Renderer* renderer, const Game& game) const;
  void drawFlippers(SDL_Renderer* renderer, const Game& game, float alpha, bool atRest) const;
  // Every ball that is in sight, those on the table first and those riding
  // a ramp's rail over them.
  void drawBalls(SDL_Renderer* renderer, std::span<const BallView> balls, bool reduceMotion) const;
  // One of drawBalls' two rounds: the balls on the table, or those up on a
  // rail (with their shadows). Table A draws its ramps between the two.
  // through: the balls alone (no shadows) at that share of their alpha,
  // for a ball seen through a ramp's plastic.
  void drawBallRound(SDL_Renderer* renderer, std::span<const BallView> balls, bool raised,
                     bool reduceMotion, float through = 1.0F) const;

  // --- Table A (concept A's art) ---------------------------------------------------
  // What is flush with the playfield and moves or lights: the altimeter's
  // pointer at the Altitude, the Prop Wash turning (its blur sheet at FL400
  // and over).
  void drawFloorA(SDL_Renderer* renderer, const Game& game, const LightShow* show) const;
  // Every insert of the art's lamp plan that is lit (art::kAInserts): the
  // rules' lamps and those the renderer lights from the game (the shots'
  // progress pips, the Altitude ladder, F-L-A-P-S, the approach lights).
  void drawInsertsA(SDL_Renderer* renderer, const Game& game, const TableLights& lights,
                    const LightShow* show) const;
  // The Flight Deck's plate, open (lit in the GI colour) or closed, its
  // flood of light and its flasher dome.
  void drawDeckA(SDL_Renderer* renderer, const Game& game, const LightShow* show) const;
  // Over the deck and the ball pools: the deck's A-C-E stand-ups and the
  // captive ball.
  void drawStandupsA(SDL_Renderer* renderer, const Game& game) const;
  void drawDoorsA(SDL_Renderer* renderer, const Game& game, const TableLights& lights) const;
  void drawPylonA(SDL_Renderer* renderer, const Game& game) const;
  void drawDroneA(SDL_Renderer* renderer, const Game& game, const LightShow* show) const;
  // The clear plastic ramps, over the balls on the table and under those
  // riding them.
  void drawRampsA(SDL_Renderer* renderer, const Game& game, const TableLights& lights,
                  const LightShow* show) const;
  // The Altitude, 0 (FL100) to 4 (FL500): the light show's, or the rules'.
  [[nodiscard]] static int altitudeOf(const Game& game, const LightShow* show) noexcept;
  // The balls' contact shadows: those on the table (on the playfield, under
  // the elements) or those up on a rail.
  void drawBallShadows(SDL_Renderer* renderer, std::span<const BallView> balls, bool raised,
                       bool reduceMotion) const;
  // The exit of a subway while a ball comes up out of it (Game::holeExitOpening):
  // a cup like the art's, faded in and out, under the balls.
  void drawExitOpenings(SDL_Renderer* renderer, const Game& game) const;

  Textures textures_;
  // The light show's drawing, with its fixed batches.
  mutable TableEffects effects_;
  // What lies outside the shell and the lane island, textured from the
  // background (makeOutsideMesh): drawn over the light show.
  std::vector<SDL_Vertex> outsideMesh_;
  bool economy_ = false;
  // What attract mode draws the table into before scaling it; made on first
  // use. Its picture is drawn again every frame, so a reset of the render
  // targets loses nothing.
  mutable TexturePtr attractTarget_;
  // Triangles for drawElements, kept between frames so that drawing one
  // allocates nothing.
  mutable std::vector<SDL_Vertex> elements_;
  // The same for revision 3's pieces (drawDrone, drawLaneGate).
  mutable std::vector<SDL_Vertex> motion_;
  // Table A's moving toys that the game shows only as up or down: when each
  // last changed, by the game's step, for the frames between (the pylon
  // rising, the bay doors dropping). Drawing only; a game started again
  // (its step count going back) starts them afresh.
  struct Toggle {
    bool on = false;
    std::uint64_t since = 0;
    bool seen = false;
    // How far through a change of the given length, 0 to 1; 1 when settled.
    float progress(bool now, std::uint64_t step, std::uint64_t length) noexcept;
  };
  mutable Toggle pylonToggle_;
  mutable Toggle doorToggle_;
};

}  // namespace pinball
