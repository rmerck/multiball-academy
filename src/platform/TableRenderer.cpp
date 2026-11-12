#include "platform/TableRenderer.hpp"

#include <cmath>
#include <cstddef>
#include <utility>

#include "core/TableLayout.hpp"
#include "platform/Canvas.hpp"

namespace pinball {

std::unique_ptr<TableRenderer> TableRenderer::create(SDL_Renderer* renderer,
                                                     const AssetLoader& assets) {
  if (renderer == nullptr) {
    return nullptr;
  }
  // A missing or damaged asset is fatal: fail at startup, not mid-game.
  Textures textures;
  textures.ball = assets.loadTexture(renderer, "sprites/ball.png");
  if (!textures.ball) {
    return nullptr;
  }
  return std::unique_ptr<TableRenderer>(new TableRenderer(std::move(textures)));
}

TableRenderer::TableRenderer(Textures textures) : textures_(std::move(textures)) {
  // Built once: the shell outline as a closed strip.
  const std::vector<Vec2> shell = table::outerBoundary();
  outline_.reserve(shell.size() + 1U);
  for (const Vec2 p : shell) {
    outline_.push_back({p.x, p.y});
  }
  if (!shell.empty()) {
    outline_.push_back({shell.front().x, shell.front().y});
  }
}

void TableRenderer::drawOutline(SDL_Renderer* r, const Game& game_) const {
  SDL_SetRenderDrawColor(r, 120, 150, 230, SDL_ALPHA_OPAQUE);

  // Outer shell, closed.
  if (outline_.size() >= 2) {
    SDL_RenderLines(r, outline_.data(), static_cast<int>(outline_.size()));
  }
  // Interior walls as their centre lines.
  for (const table::Capsule& wall : table::kWalls) {
    SDL_RenderLine(r, wall.a.x, wall.a.y, wall.b.x, wall.b.y);
  }
  const auto drawTriangle = [r](const table::Triangle& triangle) {
    for (std::size_t i = 0; i < 3; ++i) {
      const Vec2 a = triangle.points[i];
      const Vec2 b = triangle.points[(i + 1) % 3];
      SDL_RenderLine(r, a.x, a.y, b.x, b.y);
    }
  };
  for (const table::Triangle& sling : table::kSlingshots) {
    drawTriangle(sling);
  }
  drawTriangle(table::kMedalIsland);

  // Holes and rollovers as placeholder markers.
  for (const table::Hole& hole : table::kHoles) {
    SDL_SetRenderDrawColor(r, 200, 80, 200, SDL_ALPHA_OPAQUE);
    const SDL_FRect box{hole.center.x - table::kHoleRadius, hole.center.y - table::kHoleRadius,
                        table::kHoleRadius * 2.0F, table::kHoleRadius * 2.0F};
    SDL_RenderRect(r, &box);
  }
  for (const table::Rollover& rollover : table::kRollovers) {
    SDL_SetRenderDrawColor(r, 80, 200, 120, SDL_ALPHA_OPAQUE);
    const float half = rollover.zone.radius;
    const SDL_FRect box{rollover.zone.center.x - half, rollover.zone.center.y - 2.0F, half * 2.0F,
                        4.0F};
    SDL_RenderFillRect(r, &box);
  }

  // Targets: bright when lit.
  for (std::size_t i = 0; i < table::kTargets.size(); ++i) {
    const table::Capsule& c = table::kTargets[i].shape;
    if (game_.targetLit(i)) {
      SDL_SetRenderDrawColor(r, 255, 230, 90, SDL_ALPHA_OPAQUE);
    } else {
      SDL_SetRenderDrawColor(r, 150, 90, 40, SDL_ALPHA_OPAQUE);
    }
    SDL_RenderLine(r, c.a.x, c.a.y, c.b.x, c.b.y);
    SDL_RenderLine(r, c.a.x + 1.0F, c.a.y + 1.0F, c.b.x + 1.0F, c.b.y + 1.0F);
  }
}

void TableRenderer::draw(SDL_Renderer* r, const Game& game_, bool dimmed) const {
  if (r == nullptr) {
    return;
  }
  // Playfield on the left at 1:1 with table coordinates, info panel on the right.
  const SDL_FRect playfield{0.0F, 0.0F, table::kWidth, table::kHeight};
  const SDL_FRect panel{table::kWidth, 0.0F, static_cast<float>(kCanvasWidth) - table::kWidth,
                        static_cast<float>(kCanvasHeight)};
  SDL_SetRenderDrawColor(r, 10, 14, 40, SDL_ALPHA_OPAQUE);
  SDL_RenderFillRect(r, &playfield);

  // Placeholder outline until the table art lands.
  drawOutline(r, game_);

  // Bumpers as placeholder squares that brighten when hit.
  for (std::size_t i = 0; i < table::kBumpers.size(); ++i) {
    const table::Circle& c = table::kBumpers[i].shape;
    const float flash = game_.bumperFlash(i);
    const auto level = static_cast<Uint8>(90.0F + 165.0F * flash);
    SDL_SetRenderDrawColor(r, level, level, 90, SDL_ALPHA_OPAQUE);
    const float side = c.radius * 1.41421356F;
    const SDL_FRect box{c.center.x - side * 0.5F, c.center.y - side * 0.5F, side, side};
    SDL_RenderFillRect(r, &box);
  }

  // Plunger block and, when closed, the lane gate.
  const SDL_FRect plunger{table::kLaneCenterX - table::kPlungerWidth * 0.5F,
                          table::kPlungerRestTopY + game_.plungerPull(), table::kPlungerWidth,
                          table::kPlungerHeight};
  SDL_SetRenderDrawColor(r, 170, 170, 180, SDL_ALPHA_OPAQUE);
  SDL_RenderFillRect(r, &plunger);
  if (game_.laneGateClosed()) {
    SDL_RenderLine(r, table::kLaneGate.a.x, table::kLaneGate.a.y, table::kLaneGate.b.x,
                   table::kLaneGate.b.y);
  }

  // Flippers as thick placeholder lines along their centre lines.
  SDL_SetRenderDrawColor(r, 230, 120, 60, SDL_ALPHA_OPAQUE);
  for (std::size_t i = 0; i < table::kFlippers.size(); ++i) {
    const FlipperView flipper = game_.flipper(i);
    const float dx = std::cos(flipper.angle);
    const float dy = std::sin(flipper.angle);
    for (int offset = -3; offset <= 3; ++offset) {
      const float ox = -dy * static_cast<float>(offset);
      const float oy = dx * static_cast<float>(offset);
      SDL_RenderLine(r, flipper.pivot.x + ox, flipper.pivot.y + oy,
                     flipper.pivot.x + dx * table::kFlipperLength + ox,
                     flipper.pivot.y + dy * table::kFlipperLength + oy);
    }
  }

  const BallView ball = game_.ball();
  const float radius = table::kBallRadius * ball.scale;
  const SDL_FRect ballRect{ball.position.x - radius, ball.position.y - radius, radius * 2.0F,
                           radius * 2.0F};
  SDL_SetTextureAlphaMod(textures_.ball.get(), dimmed ? 128 : 255);
  if (ball.visible) {
    SDL_RenderTexture(r, textures_.ball.get(), nullptr, &ballRect);
  }

  // Drawn last so nothing on the playfield can spill over the panel.
  SDL_SetRenderDrawColor(r, 24, 24, 30, SDL_ALPHA_OPAQUE);
  SDL_RenderFillRect(r, &panel);
}

}  // namespace pinball
