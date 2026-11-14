#include "platform/TableEffects.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

#include "core/TableLayout.hpp"

namespace pinball {

namespace {

using table::LampId;

float length(Vec2 v) noexcept { return std::sqrt(v.x * v.x + v.y * v.y); }

// Where and how large a ball is drawn: a hole drawing it in or out moves
// it off its place and shrinks it (Game.hpp, BallView::drawnAt).
float drawnRadius(const BallView& ball) noexcept {
  return table::kBallRadius * ball.scale *
         (1.0F - table::kHoleSinkShrink * std::clamp(ball.sink, 0.0F, 1.0F));
}
float shown(const BallView& ball) noexcept { return 1.0F - std::clamp(ball.sink, 0.0F, 1.0F); }

Vec2 unit(Vec2 v) noexcept {
  const float l = length(v);
  return l > 0.0F ? v * (1.0F / l) : Vec2{0.0F, -1.0F};
}

// The colour of each hole's shot (generated/ArtPalette.hpp), in kHoles order.
art::Rgb holeColour(std::size_t hole) noexcept {
  switch (hole) {
    case table::kScoopHole:
      return art::kFxYellow;
    case table::kHangarHole:
      return art::kFxOrange;
    case table::kRunwayHole:
      return art::kFxViolet;
    default:
      return art::kFxMagenta;
  }
}

}  // namespace

art::Rgb mixRgb(art::Rgb a, art::Rgb b, float t) noexcept {
  t = std::clamp(t, 0.0F, 1.0F);
  const auto channel = [t](std::uint8_t from, std::uint8_t to) {
    return static_cast<std::uint8_t>(std::lround(
        static_cast<float>(from) + (static_cast<float>(to) - static_cast<float>(from)) * t));
  };
  return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b)};
}

art::Rgb fxColour(fx::Colour colour) noexcept {
  switch (colour) {
    case fx::Colour::Cyan:
      return art::kFxCyan;
    case fx::Colour::Magenta:
      return art::kFxMagenta;
    case fx::Colour::Lime:
      return art::kFxLime;
    case fx::Colour::Orange:
      return art::kFxOrange;
    case fx::Colour::Yellow:
      return art::kFxYellow;
    case fx::Colour::Violet:
      return art::kFxViolet;
    case fx::Colour::White:
      return art::kFxWhite;
    case fx::Colour::Warm:
      return art::kFxGi;
  }
  return art::kFxWhite;
}

// --- GlowBatch --------------------------------------------------------------

GlowBatch::GlowBatch() noexcept {
  for (std::size_t q = 0; q < kMaxQuads; ++q) {
    const int v = static_cast<int>(q * 4);
    const std::size_t i = q * 6;
    indices_[i + 0] = v;
    indices_[i + 1] = v + 1;
    indices_[i + 2] = v + 2;
    indices_[i + 3] = v;
    indices_[i + 4] = v + 2;
    indices_[i + 5] = v + 3;
  }
}

void GlowBatch::add(SDL_Renderer* r, SDL_Texture* texture, Vec2 centre, Vec2 half,
                    art::Rgb colour, float strength) {
  if (texture == nullptr || !(strength > 0.004F) || !(half.x > 0.0F) || !(half.y > 0.0F)) {
    return;
  }
  if (quads_ == kMaxQuads || (quads_ > 0 && texture != texture_)) {
    flush(r);
  }
  texture_ = texture;
  const SDL_FColor c{static_cast<float>(colour.r) / 255.0F, static_cast<float>(colour.g) / 255.0F,
                     static_cast<float>(colour.b) / 255.0F, std::min(strength, 1.0F)};
  SDL_Vertex* v = &vertices_[quads_ * 4];
  v[0] = {{centre.x - half.x, centre.y - half.y}, c, {0.0F, 0.0F}};
  v[1] = {{centre.x + half.x, centre.y - half.y}, c, {1.0F, 0.0F}};
  v[2] = {{centre.x + half.x, centre.y + half.y}, c, {1.0F, 1.0F}};
  v[3] = {{centre.x - half.x, centre.y + half.y}, c, {0.0F, 1.0F}};
  ++quads_;
}

void GlowBatch::flush(SDL_Renderer* r) {
  if (quads_ == 0 || texture_ == nullptr) {
    quads_ = 0;
    return;
  }
  // The vertex colours carry the tint and strength: the texture's own
  // modulation (set by other draws of it) must not.
  if (software_ == Software::Unknown) {
    const char* const name = SDL_GetRendererName(r);
    software_ = name != nullptr && std::string_view(name) == SDL_SOFTWARE_RENDERER ? Software::Yes
                                                                                   : Software::No;
  }
  if (software_ == Software::Yes) {
    // The software renderer fills a textured triangle a pixel at a time,
    // but copies a rectangle with its fast blitter: each quad as a copy.
    for (std::size_t q = 0; q < quads_; ++q) {
      const SDL_Vertex* v = &vertices_[q * 4];
      const SDL_FRect dst{v[0].position.x, v[0].position.y, v[2].position.x - v[0].position.x,
                          v[2].position.y - v[0].position.y};
      SDL_SetTextureColorModFloat(texture_, v[0].color.r, v[0].color.g, v[0].color.b);
      SDL_SetTextureAlphaModFloat(texture_, v[0].color.a);
      SDL_RenderTexture(r, texture_, nullptr, &dst);
    }
    SDL_SetTextureColorMod(texture_, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture_, 255);
    drawCalls_ += quads_;
  } else {
    SDL_SetTextureColorMod(texture_, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture_, 255);
    SDL_RenderGeometry(r, texture_, vertices_.data(), static_cast<int>(quads_ * 4),
                       indices_.data(), static_cast<int>(quads_ * 6));
    ++drawCalls_;
  }
  drawnQuads_ += quads_;
  quads_ = 0;
}

// --- TableEffects -----------------------------------------------------------

void TableEffects::flushAll(SDL_Renderer* r) {
  glow_.flush(r);
  flare_.flush(r);
  ring_.flush(r);
}

std::size_t TableEffects::quads() const noexcept {
  return glow_.drawnQuads() + flare_.drawnQuads() + ring_.drawnQuads();
}
std::size_t TableEffects::drawCalls() const noexcept {
  return glow_.drawCalls() + flare_.drawCalls() + ring_.drawCalls();
}
void TableEffects::clearCounters() noexcept {
  glow_.clearCounters();
  flare_.clearCounters();
  ring_.clearCounters();
}

void TableEffects::drawUnderlay(SDL_Renderer* r, const LightShow& show, bool lightMap) {
  const float gi = show.giLevel();
  if (gi < 0.995F) {
    // The floor darkens with the lights; lit lamps go over it untouched.
    const SDL_FRect field{0.0F, 0.0F, table::kWidth, table::kHeight};
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, art::kInk.r, art::kInk.g, art::kInk.b,
                           static_cast<Uint8>(std::lround((1.0F - gi) * 0.62F * 255.0F)));
    SDL_RenderFillRect(r, &field);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  }
  // The general illumination: warm over the whole playfield, brightest at
  // the walls and round the bumpers and slingshots (gi.png, which stops at
  // the shell).
  if (lightMap && textures_.gi != nullptr && gi > 0.0F) {
    const SDL_FRect field{0.0F, 0.0F, table::kWidth, table::kHeight};
    // In its own colours: amber walls, orange slingshots, magenta and cyan
    // under the bumpers, nothing over the open middle.
    SDL_SetTextureColorMod(textures_.gi, 255, 255, 255);
    SDL_SetTextureAlphaModFloat(textures_.gi, std::min(1.0F, art::kGiStrength * gi));
    SDL_RenderTexture(r, textures_.gi, nullptr, &field);
  }
}

void TableEffects::drawElementFlashes(SDL_Renderer* r, const Game& game, const LightShow& show) {
  SDL_Texture* const glow = textures_.glow;
  SDL_Texture* const ring = textures_.ring;
  // Turbo targets.
  for (std::size_t i = 0; i < table::kTargets.size(); ++i) {
    const float f = show.targetFlash(i);
    if (f > 0.0F) {
      const table::Capsule& shape = table::kTargets[i].shape;
      const Vec2 mid = (shape.a + shape.b) * 0.5F;
      glow_.add(r, glow, mid, 110.0F, art::kFxGi, 0.35F * f);  // The local GI pop.
      glow_.add(r, glow, mid, 72.0F, art::kFxLime, 0.85F * f);
      glow_.add(r, glow, mid, 22.0F, art::kFxWhite, 0.7F * f);
    }
  }
  // Bay doors: the face lamp, and a flash when struck or when the bank moves.
  for (std::size_t i = 0; i < table::kDropTargets.size(); ++i) {
    const table::Capsule& shape = table::kDropTargets[i];
    const Vec2 mid{(shape.a.x + shape.b.x) * 0.5F, shape.a.y};
    if (game.doorBankUp()) {
      glow_.add(r, glow, mid, 13.0F, art::kFxOrange, 0.55F * show.doorLevel(i));
    }
    const float f = show.doorFlash(i);
    if (f > 0.0F) {
      glow_.add(r, glow, mid, 56.0F, art::kFxOrange, 0.8F * f);
      glow_.add(r, glow, mid, 16.0F, art::kFxWhite, 0.6F * f);
    }
  }
  // Holes, on entry and on kickout.
  for (std::size_t i = 0; i < table::kHoles.size(); ++i) {
    const float f = show.holeFlash(i);
    if (f > 0.0F) {
      const table::Hole& hole = table::kHoles[i];
      glow_.add(r, glow, hole.center, 90.0F, holeColour(i), 0.85F * f);
      glow_.add(r, glow, hole.exitPosition, 40.0F, holeColour(i), 0.7F * f);
      ring_.add(r, ring, hole.center, 22.0F + 60.0F * (1.0F - f), holeColour(i), 0.85F * f);
    }
  }
  // The lock bays.
  for (std::size_t i = 0; i < table::kLockBays.size(); ++i) {
    const float f = show.bayFlash(i);
    if (f > 0.0F) {
      glow_.add(r, glow, table::kLockBays[i], 32.0F, art::kFxOrange, 0.65F * f);
      ring_.add(r, ring, table::kLockBays[i], 10.0F + 26.0F * (1.0F - f), art::kFxYellow, 0.7F * f);
    }
  }
  // The kickback.
  if (const float f = show.kickbackFlash(); f > 0.0F) {
    glow_.add(r, glow, table::kKickbackZone.center, 44.0F, art::kFxYellow, 0.65F * f);
    ring_.add(r, ring, table::kKickbackZone.center, 12.0F + 34.0F * (1.0F - f), art::kFxWhite,
              0.6F * f);
  }
  // Rollovers.
  for (std::size_t i = 0; i < table::kRollovers.size(); ++i) {
    const float f = show.rolloverFlash(i);
    if (f > 0.0F) {
      const bool top = table::kRollovers[i].group == table::RolloverGroup::TopLane;
      glow_.add(r, glow, table::kRollovers[i].zone.center, 40.0F,
                top ? art::kFxYellow : art::kFxCyan, 0.8F * f);
    }
  }
  // The spinner strobes with its turns.
  if (const float s = show.spinnerLevel(); s > 0.0F) {
    const Vec2 mid = (table::kSpinner.a + table::kSpinner.b) * 0.5F;
    glow_.add(r, glow, mid, {80.0F, 44.0F}, art::kFxLime, 0.8F * s);
    glow_.add(r, glow, mid, {36.0F, 12.0F}, art::kFxWhite, 0.6F * s);
  }
  // Chases: three chevrons behind each blinking arrow, running toward it.
  for (std::size_t i = 0; i < table::kLamps.size(); ++i) {
    const fx::ChaseView chase = show.chase(i);
    if (!chase.active) {
      continue;
    }
    const Vec2 at = table::kLamps[i].position;
    const auto [aimX, aimY] = art::kArrowAims[i];
    const Vec2 u = unit(Vec2{aimX, aimY} - at);
    const art::Rgb colour = art::kLampNeon[i];
    const float head = chase.phase * 4.0F;  // Runs 0 (farthest), 1, 2, then the arrow.
    for (int k = 0; k < 3; ++k) {
      const float slot = static_cast<float>(2 - k);
      const float level = chase.steady ? 0.55F : std::max(0.0F, 1.0F - std::fabs(head - slot) * 1.2F);
      if (level <= 0.0F) {
        continue;
      }
      const Vec2 p = at - u * (24.0F + 15.0F * static_cast<float>(k));
      glow_.add(r, glow, p, 16.0F, colour, 0.85F * level);
      glow_.add(r, glow, p, 6.0F, art::kFxWhite, 0.6F * level);
    }
  }
  flushAll(r);
}

void TableEffects::drawShow(SDL_Renderer* r, const Game& game, std::span<const BallView> balls,
                            float alpha, const LightShow& show) {
  SDL_Texture* const glow = textures_.glow;
  // Washes over the whole table first.
  const fx::BurstViews bursts = show.bursts();
  for (const fx::BurstView& b : bursts.span()) {
    if (b.where != fx::Flasher::Table) {
      continue;
    }
    // A soft wash from the middle out, fading before the cabinet's edge.
    glow_.add(r, glow, {table::kWidth * 0.5F, table::kHeight * 0.5F},
              {table::kWidth * 0.75F, table::kHeight * 0.75F}, fxColour(b.colour), 0.85F * b.level);
  }
  // The flasher domes.
  for (const fx::BurstView& b : bursts.span()) {
    if (b.where == fx::Flasher::Table) {
      continue;
    }
    const Vec2 at = fx::flasherPosition(b.where);
    const art::Rgb c = fxColour(b.colour);
    const float s = b.level * art::kFlasherStrengthMax;
    // flare.png carries its own bloom: a hot core over it is all it needs.
    glow_.add(r, glow, at, b.radius * fx::kDomeReach, c, s * 0.55F);  // The pool of colour it throws.
    glow_.add(r, glow, at, b.radius * 0.35F, mixRgb(c, art::kFxWhite, 0.6F), s);
    flare_.add(r, textures_.flare, at, b.radius * 1.2F, c, s);
  }
  // The multiball sweep climbing the table.
  if (const float y = show.sweepY(); y >= 0.0F) {
    const art::Rgb c = fxColour(show.sweepColour());
    glow_.add(r, glow, {table::kWidth * 0.5F, y}, {table::kWidth * 0.8F, 130.0F}, c, 0.8F);
    glow_.add(r, glow, {table::kWidth * 0.5F, y}, {table::kWidth * 0.7F, 26.0F}, art::kFxWhite, 0.6F);
  }
  // The jackpot's ring.
  if (const float radius = show.ringRadius(); radius >= 0.0F) {
    const float fade = std::max(0.0F, 1.0F - radius / 770.0F);
    // ring.png is brightest at 0.78 of its half size.
    ring_.add(r, textures_.ring, show.ringCentre(), radius / 0.78F + 12.0F, fxColour(show.ringColour()),
              0.95F * fade);
  }
  // The flippers: a soft highlight while raised.
  for (std::size_t i = 0; i < table::kFlippers.size(); ++i) {
    const table::Flipper& rest = table::kFlippers[i];
    const FlipperView f = game.flipper(i, alpha);
    const float span = rest.activeAngle - rest.restAngle;
    const float raised = std::clamp((f.angle - rest.restAngle) / span, 0.0F, 1.0F);
    if (raised > 0.02F) {
      const Vec2 dir{std::cos(f.angle), std::sin(f.angle)};
      for (const float along : {0.35F, 0.75F}) {
        glow_.add(r, glow, f.pivot + dir * (table::kFlipperLength * along), 32.0F, art::kFxMagenta,
                  0.35F * raised);
      }
    }
  }
  // Each ball's trail at speed: the motion reads as a streak.
  const float trailStrength = show.reduceMotion() ? 0.15F : 0.30F;
  for (const BallView& ball : balls) {
    if (!ball.visible || ball.locked || ball.sink > 0.0F) {
      continue;
    }
    const fx::TrailView& trail = show.trail(ball.id);
    const float fast = std::clamp((trail.speed - 4.0F) / 10.0F, 0.0F, 1.0F);
    if (trail.count < 3 || fast <= 0.0F) {
      continue;
    }
    const float radius = drawnRadius(ball);
    for (std::size_t k = 1; k < trail.count; ++k) {
      const float age = static_cast<float>(k) / static_cast<float>(trail.count);
      glow_.add(r, glow, trail.points[k], radius * (1.25F - 0.6F * age), art::kFxTrail,
                trailStrength * fast * (1.0F - age));
    }
  }
  flushAll(r);
}

void TableEffects::drawBallPools(SDL_Renderer* r, std::span<const BallView> balls,
                                 const LightShow& show) {
  SDL_Texture* const pool = textures_.pool;
  if (pool == nullptr) {
    return;
  }
  for (const BallView& ball : balls) {
    if (!ball.visible) {
      continue;
    }
    // Only as dark as the light round the ball calls for: none over the
    // plain table, deepest beside a lit lamp or a flash.
    const Vec2 at = ball.drawnAt();
    float light = 0.0F;
    for (std::size_t i = 0; i < table::kLamps.size(); ++i) {
      const float d = length(table::kLamps[i].position - at);
      if (d < 60.0F) {
        light = std::max(light, show.lampLevels()[i] * (1.0F - d / 60.0F) * 1.6F);
      }
    }
    const fx::BurstViews bursts = show.bursts();
    for (const fx::BurstView& b : bursts.span()) {
      const float reach = b.where == fx::Flasher::Table ? 1e6F : b.radius * fx::kDomeReach;
      const float d = b.where == fx::Flasher::Table ? 0.0F : length(fx::flasherPosition(b.where) - at);
      if (d < reach) {
        light = std::max(light, b.level * (1.0F - d / reach) * 1.5F);
      }
    }
    for (std::size_t i = 0; i < table::kBumpers.size(); ++i) {
      const float d = length(table::kBumpers[i].shape.center - at);
      if (d < 110.0F) {
        light = std::max(light, show.bumperFlash(i) * (1.0F - d / 110.0F) * 1.5F);
      }
    }
    const float alpha = art::kBallPoolAlpha * std::min(light, 1.0F) * shown(ball);
    if (alpha < 0.02F) {
      continue;
    }
    SDL_SetTextureAlphaModFloat(pool, alpha);
    const float radius = drawnRadius(ball) * art::kBallPoolRadii;
    const SDL_FRect dst{at.x - radius, at.y - radius, radius * 2.0F, radius * 2.0F};
    SDL_RenderTexture(r, pool, nullptr, &dst);
  }
  SDL_SetTextureAlphaMod(pool, 255);
}

void TableEffects::drawBallGlints(SDL_Renderer* r, const Game& /*game*/,
                                  std::span<const BallView> balls, const LightShow& show) {
  SDL_Texture* const glow = textures_.glow;
  // A light hanging over the bumper nest, as the backbox's would: the
  // highlight slides round the chrome as the ball rolls under it.
  const Vec2 lamp{table::kWidth * 0.5F, 240.0F};
  const fx::BurstViews bursts = show.bursts();
  for (const BallView& ball : balls) {
    if (!ball.visible) {
      continue;
    }
    const float radius = drawnRadius(ball);
    const float seen = shown(ball);
    const Vec2 here = ball.drawnAt();
    const Vec2 toLamp = unit(lamp - here);
    glow_.add(r, glow, here + toLamp * (radius * 0.45F), radius * 0.42F, art::kFxWhite, 0.5F * seen);
    // The nearest flasher or struck bumper shows on the chrome in its colour.
    float best = 0.0F;
    Vec2 from{};
    art::Rgb colour = art::kFxWhite;
    for (const fx::BurstView& b : bursts.span()) {
      if (b.where == fx::Flasher::Table) {
        continue;
      }
      const Vec2 at = fx::flasherPosition(b.where);
      const float reach = std::max(b.radius * 2.0F, 160.0F);
      const float strength = b.level * std::max(0.0F, 1.0F - length(at - here) / reach);
      if (strength > best) {
        best = strength;
        from = at;
        colour = fxColour(b.colour);
      }
    }
    for (std::size_t i = 0; i < table::kBumpers.size(); ++i) {
      const Vec2 at = table::kBumpers[i].shape.center;
      const float strength =
          show.bumperFlash(i) * std::max(0.0F, 1.0F - length(at - here) / 110.0F);
      if (strength > best) {
        best = strength;
        from = at;
        colour = art::kFxYellow;
      }
    }
    if (best > 0.02F) {
      glow_.add(r, glow, here + unit(from - here) * (radius * 0.55F), radius * 0.55F, colour,
                0.8F * best * seen);
    }
  }
  flushAll(r);
}

}  // namespace pinball
