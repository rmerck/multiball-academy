#include "platform/TableRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include "core/TableLayout.hpp"
#include "platform/Hud.hpp"

namespace pinball {

namespace {

constexpr float kRadiansToDegrees = 180.0F / table::kPi;

// Sprite geometry, in playfield pixels. These mirror tools/gimp/make_sprites.py.
constexpr float kFlipperSpriteMargin = 3.0F;
constexpr float kFlipperSpriteWidth = kFlipperSpriteMargin * 2.0F + table::kFlipperBaseRadius +
                                      table::kFlipperLength + table::kFlipperTipRadius;
constexpr float kFlipperSpriteHeight =
    kFlipperSpriteMargin * 2.0F + table::kFlipperBaseRadius * 2.0F;
constexpr float kBumperSpriteRadius = 30.0F;  // Sprite is 64 wide; the skirt is 30.
constexpr float kBarSpriteLength = 34.0F;
constexpr float kBarSpriteRadius = 5.0F;
constexpr float kBarSpriteMargin = 2.0F;

Uint8 toByte(float unit) noexcept {
  return static_cast<Uint8>(std::clamp(unit, 0.0F, 1.0F) * 255.0F + 0.5F);
}

}  // namespace

std::unique_ptr<TableRenderer> TableRenderer::create(SDL_Renderer* renderer,
                                                     const AssetLoader& assets) {
  if (renderer == nullptr) {
    return nullptr;
  }
  // A missing or damaged asset is fatal: fail at startup, not mid-game.
  Textures t;
  t.background = assets.loadTexture(renderer, "table/background.png");
  t.ball = assets.loadTexture(renderer, "sprites/ball.png");
  t.flipper = assets.loadTexture(renderer, "sprites/flipper.png");
  t.bumper = assets.loadTexture(renderer, "sprites/bumper.png");
  t.glow = assets.loadTexture(renderer, "sprites/glow.png");
  t.bar = assets.loadTexture(renderer, "sprites/bar.png");
  t.plunger = assets.loadTexture(renderer, "sprites/plunger.png");
  t.panel = assets.loadTexture(renderer, "ui/panel.png");
  if (!t.background || !t.ball || !t.flipper || !t.bumper || !t.glow || !t.bar || !t.plunger ||
      !t.panel) {
    return nullptr;
  }
  // The background is opaque: skip blending for the largest draw of the frame.
  SDL_SetTextureBlendMode(t.background.get(), SDL_BLENDMODE_NONE);
  SDL_SetTextureBlendMode(t.panel.get(), SDL_BLENDMODE_NONE);
  return std::unique_ptr<TableRenderer>(new TableRenderer(std::move(t)));
}

TableRenderer::TableRenderer(Textures textures) : textures_(std::move(textures)) {}

void TableRenderer::drawGlow(SDL_Renderer* r, Vec2 center, float radius, Rgb color,
                             float strength) const {
  if (strength <= 0.0F) {
    return;
  }
  SDL_Texture* const glow = textures_.glow.get();
  SDL_SetTextureBlendMode(glow, SDL_BLENDMODE_ADD);
  SDL_SetTextureColorMod(glow, color.r, color.g, color.b);
  SDL_SetTextureAlphaMod(glow, toByte(strength));
  const SDL_FRect dst{center.x - radius, center.y - radius, radius * 2.0F, radius * 2.0F};
  SDL_RenderTexture(r, glow, nullptr, &dst);
}

void TableRenderer::drawLights(SDL_Renderer* r, const Game& game) const {
  const Rules& rules = game.rules();
  const MissionControl& missions = rules.missions();
  const bool blinkOn = (game.stepCount() / 30U) % 2U == 0U;

  // Fuel gauge: lit from the bottom up, green fading to red as it empties.
  const int fuel = missions.fuelLights();
  for (std::size_t i = 0; i < table::kFuelLightCount; ++i) {
    // Light 0 is the top of the column and the last to go out when full.
    const int rank = static_cast<int>(table::kFuelLightCount - i);
    if (rank <= fuel) {
      const float t = static_cast<float>(i) / static_cast<float>(table::kFuelLightCount - 1);
      drawGlow(r, table::fuelLight(i), 22.0F,
               {255, static_cast<Uint8>(215.0F - 150.0F * t), 40}, 0.95F);
    }
  }

  // A selected mission waits on the ramp: pulse its entry.
  if (missions.phase() == MissionControl::Phase::Selected && blinkOn) {
    drawGlow(r, table::kRampEntry, 46.0F, {255, 160, 60}, 0.9F);
  }

  std::size_t lane = 0;
  for (const table::Rollover& rollover : table::kRollovers) {
    switch (rollover.group) {
      case table::RolloverGroup::ReEntry:
        if (rules.laneLit(lane)) {
          drawGlow(r, rollover.zone.center, 26.0F, {110, 255, 150}, 0.95F);
        }
        ++lane;
        break;
      case table::RolloverGroup::Outlane:
        if (rules.bonusLaneLit()) {
          drawGlow(r, rollover.zone.center, 26.0F, {255, 110, 90}, 0.95F);
        }
        break;
      case table::RolloverGroup::Inlane:
        break;
    }
  }
}

void TableRenderer::drawBumpers(SDL_Renderer* r, const Game& game) const {
  SDL_Texture* const cap = textures_.bumper.get();
  for (std::size_t i = 0; i < table::kBumpers.size(); ++i) {
    const table::Bumper& bumper = table::kBumpers[i];
    Rgb color{255, 110, 95};
    if (bumper.group == table::BumperGroup::Engine) {
      color = {100, 190, 255};
    } else if (bumper.group == table::BumperGroup::Satellite) {
      color = {160, 255, 130};
    }
    const float flash = game.bumperFlash(i);

    // The cap pops slightly when struck.
    const float scale = bumper.shape.radius / kBumperSpriteRadius * (1.0F + 0.08F * flash);
    const float half = 32.0F * scale;
    const SDL_FRect dst{bumper.shape.center.x - half, bumper.shape.center.y - half, half * 2.0F,
                        half * 2.0F};
    // Dim when idle, full colour when lit.
    const float level = 0.82F + 0.18F * flash;
    SDL_SetTextureColorMod(cap, toByte(static_cast<float>(color.r) / 255.0F * level),
                           toByte(static_cast<float>(color.g) / 255.0F * level),
                           toByte(static_cast<float>(color.b) / 255.0F * level));
    SDL_RenderTexture(r, cap, nullptr, &dst);
    drawGlow(r, bumper.shape.center, bumper.shape.radius * 2.6F, color, flash * 0.9F);
  }
}

void TableRenderer::drawTargets(SDL_Renderer* r, const Game& game) const {
  SDL_Texture* const bar = textures_.bar.get();
  std::size_t missionTargetsSeen = 0;
  for (std::size_t i = 0; i < table::kTargets.size(); ++i) {
    const table::Target& target = table::kTargets[i];
    Rgb color{255, 180, 50};  // Booster.
    switch (target.bank) {
      case table::TargetBank::Medal:
        color = {100, 205, 255};
        break;
      case table::TargetBank::Mission:
        color = {255, 90, 90};
        break;
      case table::TargetBank::Multiplier:
        color = {130, 255, 130};
        break;
      case table::TargetBank::Booster:
        break;
    }
    bool lit = game.targetLit(i);
    if (target.bank == table::TargetBank::Mission) {
      // The mission bank shows the choice instead: the chosen target (or all
      // three for the special mission) blinks until the mission is accepted
      // and stays lit while it runs.
      const MissionControl& missions = game.rules().missions();
      if (missions.phase() != MissionControl::Phase::Idle) {
        const bool chosen = missions.missionIndex() == kSpecialMission ||
                            missions.missionIndex() == missionTargetsSeen;
        const bool blinkOn = (game.stepCount() / 30U) % 2U == 0U;
        lit = chosen && (missions.phase() == MissionControl::Phase::Active || blinkOn);
      }
      ++missionTargetsSeen;
    }
    const float level = lit ? 1.0F : 0.38F;

    const Vec2 a = target.shape.a;
    const Vec2 b = target.shape.b;
    const Vec2 mid = (a + b) * 0.5F;
    const float length = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
    const float angle = std::atan2(b.y - a.y, b.x - a.x) * kRadiansToDegrees;

    // Stretch the bar sprite to the target's own length.
    const float spriteWidth = (kBarSpriteLength + 2.0F * (kBarSpriteRadius + kBarSpriteMargin)) *
                              (length / kBarSpriteLength);
    const float spriteHeight = 2.0F * (kBarSpriteRadius + kBarSpriteMargin);
    const SDL_FRect dst{mid.x - spriteWidth * 0.5F, mid.y - spriteHeight * 0.5F, spriteWidth,
                        spriteHeight};
    SDL_SetTextureColorMod(bar, toByte(static_cast<float>(color.r) / 255.0F * level),
                           toByte(static_cast<float>(color.g) / 255.0F * level),
                           toByte(static_cast<float>(color.b) / 255.0F * level));
    SDL_RenderTextureRotated(r, bar, nullptr, &dst, static_cast<double>(angle), nullptr,
                             SDL_FLIP_NONE);
    if (lit) {
      drawGlow(r, mid, length * 0.9F, color, 0.55F);
    }
  }
}

void TableRenderer::drawPlungerAndGate(SDL_Renderer* r, const Game& game) const {
  const SDL_FRect plunger{table::kLaneCenterX - table::kPlungerWidth * 0.5F,
                          table::kPlungerRestTopY + game.plungerPull(), table::kPlungerWidth,
                          table::kPlungerHeight};
  SDL_RenderTexture(r, textures_.plunger.get(), nullptr, &plunger);

  if (game.laneGateClosed()) {
    // A thin steel flap across the top of the lane.
    const table::Capsule& gate = table::kLaneGate;
    SDL_SetRenderDrawColor(r, 200, 210, 230, SDL_ALPHA_OPAQUE);
    for (int offset = -1; offset <= 1; ++offset) {
      const float dx = static_cast<float>(offset);
      SDL_RenderLine(r, gate.a.x + dx, gate.a.y, gate.b.x + dx, gate.b.y);
    }
  }
}

void TableRenderer::drawFlippers(SDL_Renderer* r, const Game& game) const {
  SDL_Texture* const sprite = textures_.flipper.get();
  // The pivot inside the sprite: the centre of the base circle.
  const SDL_FPoint pivotInSprite{kFlipperSpriteMargin + table::kFlipperBaseRadius,
                                 kFlipperSpriteHeight * 0.5F};
  for (std::size_t i = 0; i < table::kFlippers.size(); ++i) {
    const FlipperView flipper = game.flipper(i);
    const SDL_FRect dst{flipper.pivot.x - pivotInSprite.x, flipper.pivot.y - pivotInSprite.y,
                        kFlipperSpriteWidth, kFlipperSpriteHeight};
    // The right flipper points left, so its sprite is mirrored top to bottom
    // to keep the highlight on the upper edge.
    const SDL_FlipMode flip = i == table::kRightFlipperIndex ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE;
    SDL_RenderTextureRotated(r, sprite, nullptr, &dst,
                             static_cast<double>(flipper.angle * kRadiansToDegrees),
                             &pivotInSprite, flip);
  }
}

void TableRenderer::drawBall(SDL_Renderer* r, const Game& game) const {
  const BallView ball = game.ball();
  if (!ball.visible) {
    return;
  }
  const float radius = table::kBallRadius * ball.scale;

  // Soft contact shadow, offset further when the ball is up on the ramp.
  SDL_Texture* const glow = textures_.glow.get();
  const float lift = (ball.scale - 1.0F) * 40.0F;
  SDL_SetTextureBlendMode(glow, SDL_BLENDMODE_BLEND);
  SDL_SetTextureColorMod(glow, 0, 0, 0);
  SDL_SetTextureAlphaMod(glow, 150);
  const float shadowRadius = radius * 1.55F;
  const SDL_FRect shadow{ball.position.x - shadowRadius + 3.0F + lift,
                         ball.position.y - shadowRadius + 4.0F + lift, shadowRadius * 2.0F,
                         shadowRadius * 2.0F};
  SDL_RenderTexture(r, glow, nullptr, &shadow);

  const SDL_FRect dst{ball.position.x - radius, ball.position.y - radius, radius * 2.0F,
                      radius * 2.0F};
  SDL_RenderTexture(r, textures_.ball.get(), nullptr, &dst);
}

void TableRenderer::draw(SDL_Renderer* r, const Game& game, bool dimmed) const {
  if (r == nullptr) {
    return;
  }
  const SDL_FRect playfield{0.0F, 0.0F, table::kWidth, table::kHeight};

  // The playfield is drawn in its own viewport, which also clips it so that
  // nothing spills onto the panel. A nudge shifts the viewport a few pixels.
  const Vec2 shake = game.shake();
  const SDL_Rect view{static_cast<int>(std::lround(std::clamp(shake.x, -12.0F, 12.0F))),
                      static_cast<int>(std::lround(std::clamp(shake.y, -12.0F, 12.0F))),
                      static_cast<int>(table::kWidth), static_cast<int>(table::kHeight)};
  SDL_SetRenderViewport(r, &view);

  SDL_RenderTexture(r, textures_.background.get(), nullptr, &playfield);
  drawLights(r, game);
  drawTargets(r, game);
  drawBumpers(r, game);
  drawPlungerAndGate(r, game);
  drawFlippers(r, game);
  drawBall(r, game);

  if (dimmed) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 10, 130);
    SDL_RenderFillRect(r, &playfield);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  }
  SDL_SetRenderViewport(r, nullptr);
  // The panel covers anything the shifted playfield drew past its right edge.

  drawHud(r, textures_.panel.get(), game);
}

}  // namespace pinball
