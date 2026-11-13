#include "platform/TableRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "core/AssetManifest.hpp"
#include "core/TableLayout.hpp"
#include "platform/EmbeddedManifest.hpp"
#include "platform/GameViews.hpp"
#include "platform/Hud.hpp"

namespace pinball {

namespace {

constexpr float kRadiansToDegrees = 180.0F / table::kPi;

// Sprite geometry, in playfield pixels. These mirror tools/gimp/make_sprites.py.
constexpr float kFlipperSpriteMargin = 9.0F;  // Room for the bat's halo.
constexpr float kFlipperSpriteWidth = kFlipperSpriteMargin * 2.0F + table::kFlipperBaseRadius +
                                      table::kFlipperLength + table::kFlipperTipRadius;
constexpr float kFlipperSpriteHeight =
    kFlipperSpriteMargin * 2.0F + table::kFlipperBaseRadius * 2.0F;
constexpr float kBumperSpriteRadius = 30.0F;  // Sprite is 64 wide; the skirt is 30.
constexpr float kBarSpriteLength = 34.0F;
constexpr float kBarSpriteRadius = 5.0F;
constexpr float kBarSpriteMargin = 2.0F;

Uint8 toByte(float unit) noexcept {
  // Written so that not-a-number takes the first branch: std::clamp would
  // pass it through, and converting it to an integer is undefined.
  if (!(unit > 0.0F)) {
    return 0;
  }
  return static_cast<Uint8>(std::min(unit, 1.0F) * 255.0F + 0.5F);
}

// Where a bar sprite goes: the turbo targets, then the bay doors. They never
// move, so this is worked out once instead of every frame.
struct BarSprite {
  SDL_FRect dst;
  double angleDegrees;
};
constexpr std::size_t kBarCount = table::kTargets.size() + table::kDropTargets.size();

std::array<BarSprite, kBarCount> makeBarSprites() noexcept {
  std::array<BarSprite, kBarCount> sprites{};
  for (std::size_t i = 0; i < sprites.size(); ++i) {
    const table::Capsule& shape = i < table::kTargets.size()
                                      ? table::kTargets[i].shape
                                      : table::kDropTargets[i - table::kTargets.size()];
    const Vec2 a = shape.a;
    const Vec2 b = shape.b;
    const Vec2 mid = (a + b) * 0.5F;
    const float length = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
    // Stretch the bar sprite to the bar's own length.
    const float spriteWidth = (kBarSpriteLength + 2.0F * (kBarSpriteRadius + kBarSpriteMargin)) *
                              (length / kBarSpriteLength);
    const float spriteHeight = 2.0F * (kBarSpriteRadius + kBarSpriteMargin);
    sprites[i] = {
        .dst = {mid.x - spriteWidth * 0.5F, mid.y - spriteHeight * 0.5F, spriteWidth,
                spriteHeight},
        .angleDegrees =
            static_cast<double>(std::atan2(b.y - a.y, b.x - a.x) * kRadiansToDegrees)};
  }
  return sprites;
}

// --- The table drawn from its layout -------------------------------------------------
// Plain colours, no art: what a build shows until the playfield art for the
// layout exists. Everything is turned into triangles once.

constexpr SDL_FColor rgb(int r, int g, int b) noexcept {
  return {static_cast<float>(r) / 255.0F, static_cast<float>(g) / 255.0F,
          static_cast<float>(b) / 255.0F, 1.0F};
}
constexpr SDL_FColor kOutsideColor = rgb(10, 0, 26);    // Off the playfield.
constexpr SDL_FColor kFieldColor = rgb(36, 27, 74);     // Where the ball rolls.
constexpr SDL_FColor kWallColor = rgb(40, 210, 200);    // Walls and guides: teal.
constexpr SDL_FColor kDeadColor = rgb(176, 176, 190);   // Dead posts and vanes: metal.
constexpr SDL_FColor kSlingColor = rgb(232, 60, 150);   // Slingshots: hot pink.
constexpr SDL_FColor kSwitchColor = rgb(90, 255, 180);  // Rollover strips.
constexpr SDL_FColor kHoleColor = rgb(255, 150, 40);    // Scoop and hangar boxes.
constexpr SDL_FColor kRampColor = rgb(255, 225, 70);    // Ramp mouths and rails.
constexpr SDL_FColor kLampColor = rgb(120, 96, 176);    // Unlit lamps.
constexpr SDL_FColor kMarkColor = rgb(84, 64, 132);     // Spinner, kickback, lock bays.
constexpr SDL_FColor kLitColor = rgb(255, 240, 120);    // A lamp that is on.
constexpr SDL_FColor kBladeColor = rgb(240, 240, 255);  // The spinner's blade.
constexpr SDL_FColor kDoorDownColor = rgb(120, 70, 20); // Where a dropped door lies.

class Mesh {
 public:
  explicit Mesh(std::vector<SDL_Vertex>& out) noexcept : out_(out) {}

  void triangle(Vec2 a, Vec2 b, Vec2 c, SDL_FColor color) {
    for (const Vec2 p : {a, b, c}) {
      out_.push_back({{p.x, p.y}, color, {0.0F, 0.0F}});
    }
  }
  // A filled circle as a fan.
  void disc(Vec2 centre, float radius, SDL_FColor color) {
    constexpr int kSides = 20;
    Vec2 previous{centre.x + radius, centre.y};
    for (int i = 1; i <= kSides; ++i) {
      const float angle = 2.0F * table::kPi * static_cast<float>(i) / static_cast<float>(kSides);
      const Vec2 next{centre.x + radius * std::cos(angle), centre.y + radius * std::sin(angle)};
      triangle(centre, previous, next, color);
      previous = next;
    }
  }
  // A straight line of a width, square ended.
  void line(Vec2 a, Vec2 b, float width, SDL_FColor color) {
    const Vec2 d = b - a;
    const float length = std::sqrt(d.x * d.x + d.y * d.y);
    if (!(length > 0.0F)) {
      return;
    }
    const Vec2 n{-d.y / length * width * 0.5F, d.x / length * width * 0.5F};
    triangle(a + n, b + n, b - n, color);
    triangle(a + n, b - n, a - n, color);
  }
  // A thick line with round ends: a wall as the physics has it.
  void capsule(const table::Capsule& c, SDL_FColor color) {
    line(c.a, c.b, 2.0F * c.radius, color);
    disc(c.a, c.radius, color);
    disc(c.b, c.radius, color);
  }
  void polyline(std::span<const Vec2> points, float width, SDL_FColor color, bool closed) {
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
      line(points[i], points[i + 1], width, color);
      disc(points[i + 1], width * 0.5F, color);
    }
    if (closed && points.size() > 2) {
      line(points.back(), points.front(), width, color);
      disc(points.front(), width * 0.5F, color);
    }
  }
  // The outline of a box.
  void box(Vec2 centre, Vec2 half, float width, SDL_FColor color) {
    const std::array<Vec2, 4> corners{{{centre.x - half.x, centre.y - half.y},
                                       {centre.x + half.x, centre.y - half.y},
                                       {centre.x + half.x, centre.y + half.y},
                                       {centre.x - half.x, centre.y + half.y}}};
    polyline(corners, width, color, true);
  }
  void ring(Vec2 centre, float radius, float width, SDL_FColor color) {
    constexpr int kSides = 20;
    std::array<Vec2, kSides> points{};
    for (int i = 0; i < kSides; ++i) {
      const float angle = 2.0F * table::kPi * static_cast<float>(i) / static_cast<float>(kSides);
      points[static_cast<std::size_t>(i)] = {centre.x + radius * std::cos(angle),
                                             centre.y + radius * std::sin(angle)};
    }
    polyline(points, width, color, true);
  }
  // A simple polygon, filled, by clipping ears. Either winding.
  void polygon(std::span<const Vec2> outline, SDL_FColor color) {
    std::vector<Vec2> points(outline.begin(), outline.end());
    const auto cross = [](Vec2 o, Vec2 a, Vec2 b) {
      return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };
    float doubleArea = 0.0F;
    for (std::size_t i = 0; i < points.size(); ++i) {
      const Vec2 a = points[i];
      const Vec2 b = points[(i + 1) % points.size()];
      doubleArea += a.x * b.y - b.x * a.y;
    }
    const float turn = doubleArea >= 0.0F ? 1.0F : -1.0F;
    while (points.size() > 3) {
      bool clipped = false;
      for (std::size_t i = 0; i < points.size() && !clipped; ++i) {
        const Vec2 a = points[(i + points.size() - 1) % points.size()];
        const Vec2 b = points[i];
        const Vec2 c = points[(i + 1) % points.size()];
        if (cross(a, b, c) * turn <= 0.0F) {
          continue;  // A reflex corner is not an ear.
        }
        bool empty = true;
        for (const Vec2 p : points) {
          const bool corner = (p.x == a.x && p.y == a.y) || (p.x == b.x && p.y == b.y) ||
                              (p.x == c.x && p.y == c.y);
          if (!corner && cross(a, b, p) * turn >= 0.0F && cross(b, c, p) * turn >= 0.0F &&
              cross(c, a, p) * turn >= 0.0F) {
            empty = false;
            break;
          }
        }
        if (empty) {
          triangle(a, b, c, color);
          points.erase(points.begin() + static_cast<std::ptrdiff_t>(i));
          clipped = true;
        }
      }
      if (!clipped) {
        return;  // Not a simple polygon: leave the rest unfilled.
      }
    }
    if (points.size() == 3) {
      triangle(points[0], points[1], points[2], color);
    }
  }

 private:
  std::vector<SDL_Vertex>& out_;
};

std::vector<SDL_Vertex> makePlayfieldMesh() {
  std::vector<SDL_Vertex> vertices;
  Mesh mesh(vertices);
  constexpr float kChainWidth = 4.0F;

  // The field inside the shell, with the lane island cut back out of it.
  mesh.polygon(table::kShell, kFieldColor);
  mesh.polygon(table::kLaneIsland, kOutsideColor);
  mesh.polyline(table::kShell, kChainWidth, kWallColor, true);
  mesh.polyline(table::kLaneIsland, kChainWidth, kWallColor, true);
  // The inner arch without its ghost points; the vanes likewise.
  mesh.polyline(std::span(table::kInnerArch).subspan(1, table::kInnerArch.size() - 2), kChainWidth,
                kWallColor, false);
  for (const auto& vane : {table::kSlingVaneLeft, table::kSlingVaneRight}) {
    mesh.polyline(std::span(vane).subspan(1, vane.size() - 2), 2.0F, kDeadColor, false);
  }

  // Marks under everything that stands on the table: lamps, switches, the
  // boxes that take the ball, the rails.
  for (const table::Lamp& lamp : table::kLamps) {
    const Vec2 at = lamp.position;
    if (table::isArrowLamp(lamp.id)) {
      // An arrow pointing up the table at its shot.
      const float half = table::kArrowLampLength * 0.5F;
      mesh.triangle({at.x, at.y - half}, {at.x + half * 0.7F, at.y + half},
                    {at.x - half * 0.7F, at.y + half}, kLampColor);
    } else {
      const float radius = (table::isCourseLamp(lamp.id) ? table::kCourseLampDiameter
                                                          : table::kStatusLampDiameter) *
                           0.5F;
      mesh.disc(at, radius, kLampColor);
    }
  }
  for (const table::Rollover& rollover : table::kRollovers) {
    mesh.box(rollover.zone.center, table::rolloverHalfSize(rollover), 2.0F, kSwitchColor);
  }
  for (const Vec2 bay : table::kLockBays) {
    mesh.ring(bay, table::kBallRadius, 2.0F, kMarkColor);
  }
  for (const table::Hole& hole : table::kHoles) {
    const bool ramp = table::isRamp(hole.type);
    mesh.box(hole.center, hole.halfSize, 2.0F, ramp ? kRampColor : kHoleColor);
    if (ramp) {
      mesh.polyline(table::rampRail(hole.type), 3.0F, kRampColor, false);
    }
    // Where the ball comes back.
    mesh.ring(hole.exitPosition, 5.0F, 2.0F, ramp ? kRampColor : kHoleColor);
  }
  mesh.line(table::kSpinner.a, table::kSpinner.b, 3.0F, kMarkColor);
  mesh.ring(table::kKickbackZone.center, table::kKickbackZone.radius, 2.0F, kMarkColor);

  for (const table::Capsule& wall : table::kAllWalls) {
    mesh.capsule(wall, kWallColor);
  }
  for (const table::Capsule& post : table::kDeadPosts) {
    mesh.capsule(post, kDeadColor);
  }
  for (const table::Triangle& sling : table::kSlingshots) {
    mesh.triangle(sling.points[0], sling.points[1], sling.points[2], kSlingColor);
    mesh.polyline(sling.points, 2.0F * sling.cornerRadius, kSlingColor, true);
  }
  return vertices;
}

// The layout drawing as a texture of the size the art has (two pixels to a
// table unit), drawn once: a frame then costs one copy, as it does with art,
// and not some nine thousand triangles. A target texture loses what is in
// it when the device is reset or the targets are; the application makes the
// renderer again on either event.
TexturePtr makePlayfieldTexture(SDL_Renderer* r) {
  constexpr int kScale = 2;
  TexturePtr texture(SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
                                       static_cast<int>(table::kWidth) * kScale,
                                       static_cast<int>(table::kHeight) * kScale));
  SDL_Texture* const before = SDL_GetRenderTarget(r);
  if (!texture || !SDL_SetRenderTarget(r, texture.get())) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not make the playfield texture: %s",
                 SDL_GetError());
    return nullptr;
  }
  // A render target has its own scale, viewport and clip: nothing of the
  // window's is disturbed.
  const std::vector<SDL_Vertex> mesh = makePlayfieldMesh();
  bool drawn = SDL_SetRenderScale(r, static_cast<float>(kScale), static_cast<float>(kScale));
  drawn = SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE) && drawn;
  drawn = SDL_SetRenderDrawColor(r, 10, 0, 26, SDL_ALPHA_OPAQUE) && drawn;
  drawn = SDL_RenderClear(r) && drawn;
  drawn = SDL_RenderGeometry(r, nullptr, mesh.data(), static_cast<int>(mesh.size()), nullptr, 0) &&
          drawn;
  drawn = SDL_SetRenderTarget(r, before) && drawn;
  if (!drawn) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not draw the playfield texture: %s",
                 SDL_GetError());
    return nullptr;
  }
  return texture;
}

}  // namespace

std::unique_ptr<TableRenderer> TableRenderer::create(SDL_Renderer* renderer,
                                                     const AssetLoader& assets,
                                                     TextureSet set) {
  if (renderer == nullptr) {
    return nullptr;
  }
  // A missing or damaged asset is fatal: fail at startup, not mid-game.
  Textures t;
  // Art that may not have been drawn yet. One the manifest lists must load
  // like any other; one it does not list is simply not there.
  bool failed = false;
  const auto optional = [&](std::string_view name) -> TexturePtr {
    if (findAsset(embeddedAssets(), name) == nullptr) {
      return nullptr;
    }
    TexturePtr texture = assets.loadTexture(renderer, name);
    failed = failed || !texture;
    return texture;
  };
  // The playfield art belongs to one layout of the table. A build without
  // it draws the table from the layout itself.
  t.background = optional("table/background.png");
  if (!t.background && !failed) {
    t.background = makePlayfieldTexture(renderer);
    failed = !t.background;
    t.backgroundFromLayout = true;
  }
  t.ball = assets.loadTexture(renderer, "sprites/ball.png");
  t.flipper = assets.loadTexture(renderer, "sprites/flipper.png");
  t.bumper = assets.loadTexture(renderer, "sprites/bumper.png");
  t.glow = assets.loadTexture(renderer, "sprites/glow.png");
  t.bar = assets.loadTexture(renderer, "sprites/bar.png");
  t.plunger = assets.loadTexture(renderer, "sprites/plunger.png");
  t.panel = assets.loadTexture(renderer, "ui/panel.png");
  if (failed || !t.ball || !t.flipper || !t.bumper || !t.glow || !t.bar || !t.plunger ||
      !t.panel) {
    return nullptr;
  }
  if (set == TextureSet::All) {
    t.strip = optional("ui/strip.png");
    t.deck = optional("ui/deck.png");
    t.backdrop = optional("ui/backdrop.png");
    t.title = optional("ui/title.png");
    t.button = optional("sprites/button.png");
    if (failed) {
      return nullptr;
    }
    for (SDL_Texture* opaque : {t.strip.get(), t.deck.get(), t.backdrop.get()}) {
      if (opaque != nullptr) {
        SDL_SetTextureBlendMode(opaque, SDL_BLENDMODE_NONE);
      }
    }
  }
  // The background is opaque: skip blending for the largest draw of the frame.
  SDL_SetTextureBlendMode(t.background.get(), SDL_BLENDMODE_NONE);
  SDL_SetTextureBlendMode(t.panel.get(), SDL_BLENDMODE_NONE);
  // The glow is additive everywhere except the ball's shadow, which switches
  // it for one draw and switches it back.
  SDL_SetTextureBlendMode(t.glow.get(), SDL_BLENDMODE_ADD);
  return std::unique_ptr<TableRenderer>(new TableRenderer(std::move(t)));
}

TableRenderer::TableRenderer(Textures textures) : textures_(std::move(textures)) {}

void TableRenderer::drawGlow(SDL_Renderer* r, Vec2 center, float radius, Rgb color,
                             float strength) const {
  if (strength <= 0.0F) {
    return;
  }
  SDL_Texture* const glow = textures_.glow.get();
  SDL_SetTextureColorMod(glow, color.r, color.g, color.b);
  SDL_SetTextureAlphaMod(glow, toByte(strength));
  const SDL_FRect dst{center.x - radius, center.y - radius, radius * 2.0F, radius * 2.0F};
  SDL_RenderTexture(r, glow, nullptr, &dst);
}

void TableRenderer::drawBumpers(SDL_Renderer* r, const Game& game) const {
  SDL_Texture* const cap = textures_.bumper.get();
  for (std::size_t i = 0; i < table::kBumpers.size(); ++i) {
    const table::Bumper& bumper = table::kBumpers[i];
    // The thrusters are all one colour.
    const Rgb color{40, 232, 255};
    const float flash = game.bumperFlash(i);

    // The cap pops slightly when struck.
    const float scale = bumper.shape.radius / kBumperSpriteRadius * (1.0F + 0.08F * flash);
    const float half = 32.0F * scale;
    const SDL_FRect dst{bumper.shape.center.x - half, bumper.shape.center.y - half, half * 2.0F,
                        half * 2.0F};
    // Dim when idle, full colour when lit.
    const float level = 0.90F + 0.10F * flash;
    SDL_SetTextureColorMod(cap, toByte(static_cast<float>(color.r) / 255.0F * level),
                           toByte(static_cast<float>(color.g) / 255.0F * level),
                           toByte(static_cast<float>(color.b) / 255.0F * level));
    SDL_RenderTexture(r, cap, nullptr, &dst);
    // A struck bumper blooms: a wide halo in its colour and a white-hot core.
    drawGlow(r, bumper.shape.center, bumper.shape.radius * 3.0F, color, flash * 0.9F);
    drawGlow(r, bumper.shape.center, bumper.shape.radius * 1.3F, {255, 255, 255}, flash * 0.7F);
  }
}

void TableRenderer::drawElements(SDL_Renderer* r, const Game& game) const {
  elements_.clear();
  Mesh mesh(elements_);
  // Doors that have dropped lie flat: an outline where each stood.
  if (!game.doorBankUp()) {
    for (const table::Capsule& door : table::kDropTargets) {
      mesh.box((door.a + door.b) * 0.5F,
               {(door.b.x - door.a.x) * 0.5F + door.radius, door.radius}, 2.0F, kDoorDownColor);
    }
  }
  // Lamps that the table itself answers for: a lock lamp for each ball in
  // a bay, and the kickback's while it is armed.
  for (const table::Lamp& lamp : table::kLamps) {
    const bool lit =
        (lamp.id == table::LampId::Kickback && game.kickbackArmed()) ||
        (lamp.id == table::LampId::Lock1 && game.lockedBalls() >= 1) ||
        (lamp.id == table::LampId::Lock2 && game.lockedBalls() >= 2);
    if (lit) {
      mesh.disc(lamp.position, table::kStatusLampDiameter * 0.5F, kLitColor);
    }
  }
  // The spinner's blade seen from above: full depth when it lies flat, an
  // edge when it stands, twice a turn.
  const float turn = 2.0F * table::kPi * game.spinnerPhase();
  constexpr float kBladeDepth = 16.0F;
  mesh.line(table::kSpinner.a, table::kSpinner.b,
            2.0F + (kBladeDepth - 2.0F) * std::fabs(std::cos(turn)), kBladeColor);
  SDL_RenderGeometry(r, nullptr, elements_.data(), static_cast<int>(elements_.size()), nullptr, 0);
}

void TableRenderer::drawTargets(SDL_Renderer* r, const Game& game) const {
  SDL_Texture* const bar = textures_.bar.get();
  static const std::array<BarSprite, kBarCount> kSprites = makeBarSprites();
  for (std::size_t i = 0; i < kSprites.size(); ++i) {
    // Turbo targets mint, bay doors orange. A door is the dim tube of an
    // unlit bar until it has been hit, bright once it is marked, and not
    // drawn at all while the bank is down.
    const bool door = i >= table::kTargets.size();
    if (door && !game.doorBankUp()) {
      continue;
    }
    const Rgb color = door ? Rgb{255, 150, 40} : Rgb{77, 255, 178};
    const float level =
        door && game.rules().doorMarked(i - table::kTargets.size()) ? 1.0F : 0.6F;
    SDL_SetTextureColorMod(bar, toByte(static_cast<float>(color.r) / 255.0F * level),
                           toByte(static_cast<float>(color.g) / 255.0F * level),
                           toByte(static_cast<float>(color.b) / 255.0F * level));
    SDL_RenderTextureRotated(r, bar, nullptr, &kSprites[i].dst, kSprites[i].angleDegrees, nullptr,
                             SDL_FLIP_NONE);
  }
}

void TableRenderer::drawSlingshotFlashes(SDL_Renderer* r, const Game& game) const {
  for (std::size_t i = 0; i < table::kSlingshots.size(); ++i) {
    // On the middle of the kicking face.
    const table::Triangle& sling = table::kSlingshots[i];
    drawGlow(r, (sling.points[0] + sling.points[2]) * 0.5F, 60.0F, {255, 60, 170},
             game.slingshotFlash(i) * 0.8F);
  }
}

void TableRenderer::drawPlunger(SDL_Renderer* r, const Game& game) const {
  const SDL_FRect plunger{table::kLaneCenterX - table::kPlungerWidth * 0.5F,
                          table::kPlungerRestTopY + game.plungerPull(), table::kPlungerWidth,
                          table::kPlungerHeight};
  SDL_RenderTexture(r, textures_.plunger.get(), nullptr, &plunger);
}

void TableRenderer::drawFlippers(SDL_Renderer* r, const Game& game, float alpha) const {
  SDL_Texture* const sprite = textures_.flipper.get();
  // The pivot inside the sprite: the centre of the base circle.
  const SDL_FPoint pivotInSprite{kFlipperSpriteMargin + table::kFlipperBaseRadius,
                                 kFlipperSpriteHeight * 0.5F};
  for (std::size_t i = 0; i < table::kFlippers.size(); ++i) {
    const FlipperView flipper = game.flipper(i, alpha);
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

void TableRenderer::drawBalls(SDL_Renderer* r, std::span<const BallView> balls) const {
  SDL_Texture* const glow = textures_.glow.get();
  // Two rounds, so that a ball up on a rail passes over one on the table.
  // Within a round all the shadows go down first, then the halos, then the
  // balls themselves.
  for (const bool raised : {false, true}) {
    const auto inRound = [raised](const BallView& ball) noexcept {
      return ball.visible && ball.raised == raised;
    };
    if (std::ranges::none_of(balls, inRound)) {
      continue;
    }
    // Soft contact shadows, offset further when the ball is up on a rail.
    // The glow texture is additive everywhere else: switched once for all of
    // them, and back.
    SDL_SetTextureBlendMode(glow, SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(glow, 0, 0, 0);
    SDL_SetTextureAlphaMod(glow, 150);
    for (const BallView& ball : balls) {
      if (!inRound(ball)) {
        continue;
      }
      const float radius = table::kBallRadius * ball.scale;
      const float lift = (ball.scale - 1.0F) * 40.0F;
      const float shadowRadius = radius * 1.55F;
      const SDL_FRect shadow{ball.position.x - shadowRadius + 3.0F + lift,
                             ball.position.y - shadowRadius + 4.0F + lift, shadowRadius * 2.0F,
                             shadowRadius * 2.0F};
      SDL_RenderTexture(r, glow, nullptr, &shadow);
    }
    SDL_SetTextureBlendMode(glow, SDL_BLENDMODE_ADD);

    // A cool halo keeps the chrome ball easy to follow over the busy art.
    for (const BallView& ball : balls) {
      if (inRound(ball)) {
        drawGlow(r, ball.position, table::kBallRadius * ball.scale * 2.3F, {150, 225, 255}, 0.45F);
      }
    }

    for (const BallView& ball : balls) {
      if (!inRound(ball)) {
        continue;
      }
      const float radius = table::kBallRadius * ball.scale;
      const SDL_FRect dst{ball.position.x - radius, ball.position.y - radius, radius * 2.0F,
                          radius * 2.0F};
      SDL_RenderTexture(r, textures_.ball.get(), nullptr, &dst);
    }
  }
}

void TableRenderer::drawBackdrop(SDL_Renderer* r, const screen::Layout& layout) const {
  if (r == nullptr || layout.idiom == screen::Idiom::Desktop) {
    return;
  }
  const SDL_FRect whole{0.0F, 0.0F, layout.canvasWidth, layout.canvasHeight};
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(r, screen::kBackdropRgb[0], screen::kBackdropRgb[1],
                         screen::kBackdropRgb[2], SDL_ALPHA_OPAQUE);
  SDL_RenderFillRect(r, &whole);
  if (SDL_Texture* const tile = textures_.backdrop.get()) {
    // 512 pixels to 256 units, from the canvas origin.
    SDL_RenderTextureTiled(r, tile, nullptr, 0.5F, &whole);
    return;
  }
  // No art yet: a lattice of dim dots, left out where the composition will
  // cover it anyway.
  const screen::Rect& field = layout.playfield;
  const bool stacked = layout.composition == screen::Composition::Stack;
  const float coveredLeft = field.x;
  const float coveredRight = stacked ? field.x + field.w : layout.hud.x + layout.hud.w;
  const float coveredTop = stacked ? layout.hud.y : field.y;
  const float coveredBottom = stacked ? layout.deck.y + layout.deck.h : field.y + field.h;
  constexpr float kPitch = 32.0F;
  constexpr float kDot = 4.0F;
  SDL_SetRenderDrawColor(r, 38, 16, 78, SDL_ALPHA_OPAQUE);
  // Handed over in batches: one call for hundreds of dots, not one each.
  std::array<SDL_FRect, 256> dots{};
  std::size_t count = 0;
  const auto flush = [&]() {
    if (count > 0) {
      SDL_RenderFillRects(r, dots.data(), static_cast<int>(count));
      count = 0;
    }
  };
  int row = 0;
  for (float y = kPitch * 0.5F; y < layout.canvasHeight; y += kPitch * 0.5F, ++row) {
    const bool rowCovered = y >= coveredTop && y + kDot <= coveredBottom;
    for (float x = row % 2 == 0 ? kPitch * 0.5F : kPitch; x < layout.canvasWidth; x += kPitch) {
      if (rowCovered && x >= coveredLeft && x + kDot <= coveredRight) {
        // Skip to the far side of the composition.
        x += std::floor((coveredRight - x) / kPitch) * kPitch;
        continue;
      }
      dots[count++] = {x, y, kDot, kDot};
      if (count == dots.size()) {
        flush();
      }
    }
  }
  flush();
}

void TableRenderer::draw(SDL_Renderer* r, const Game& game, float alpha,
                         const screen::Layout& layout, const DrawOptions& options,
                         const HudExtras& extras) const {
  if (r == nullptr) {
    return;
  }
  // In the playfield's own coordinates: everything on the table is drawn
  // inside a viewport placed at the layout's playfield.
  const SDL_FRect playfield{0.0F, 0.0F, table::kWidth, table::kHeight};
  const int left = static_cast<int>(std::lround(layout.playfield.x));
  const int top = static_cast<int>(std::lround(layout.playfield.y));

  // The viewport also clips the table so that nothing spills onto the rest
  // of the canvas. A nudge shifts the viewport a few pixels, unless the
  // player has asked for less motion.
  const Vec2 shake = options.reduceMotion ? Vec2{} : game.shake();
  const int shakeX = static_cast<int>(std::lround(std::clamp(shake.x, -12.0F, 12.0F)));
  const int shakeY = static_cast<int>(std::lround(std::clamp(shake.y, -12.0F, 12.0F)));
  const SDL_Rect view{left + shakeX, top + shakeY, static_cast<int>(table::kWidth),
                      static_cast<int>(table::kHeight)};
  SDL_SetRenderViewport(r, &view);
  // The shifted viewport may reach past the playfield's place on the canvas,
  // and at the canvas edge on a window of another shape that is the
  // letterbox. Clip to the playfield's own rectangle; a clip rectangle is
  // relative to the viewport.
  const SDL_Rect clip{-shakeX, -shakeY, view.w, view.h};
  SDL_SetRenderClipRect(r, &clip);

  SDL_RenderTexture(r, textures_.background.get(), nullptr, &playfield);
  if (textures_.backgroundFromLayout) {
    drawElements(r, game);
  }
  drawTargets(r, game);
  drawBumpers(r, game);
  drawSlingshotFlashes(r, game);
  drawPlunger(r, game);
  drawFlippers(r, game, alpha);
  {
    // The shadows and the halos follow the same in-between positions as
    // the balls.
    const DrawnBalls balls = ballsToDraw(game, alpha, options.extraBalls);
    drawBalls(r, balls.span());
  }

  if (options.dimmed) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 10, 0, 26, 140);
    SDL_RenderFillRect(r, &playfield);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  }
  SDL_SetRenderClipRect(r, nullptr);
  SDL_SetRenderViewport(r, nullptr);

  drawHud(r,
          HudArt{.panel = textures_.panel.get(),
                 .strip = textures_.strip.get(),
                 .title = textures_.title.get()},
          game, layout, extras);
}

}  // namespace pinball
