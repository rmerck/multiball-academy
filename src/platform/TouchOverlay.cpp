#include "platform/TouchOverlay.hpp"

#include <algorithm>
#include <string_view>

#include "core/PanelLayout.hpp"
#include "platform/TableRenderer.hpp"
#include "platform/Text.hpp"

namespace pinball {

namespace {

constexpr SDL_Color kAmber{255, 225, 70, 255};
constexpr SDL_Color kCyan{70, 235, 255, 255};
constexpr SDL_Color kGreen{90, 255, 180, 255};
constexpr SDL_Color kPink{255, 60, 170, 255};
constexpr SDL_Color kViolet{150, 90, 255, 255};
constexpr SDL_Color kWhite{248, 240, 255, 255};

void fill(SDL_Renderer* r, float x, float y, float w, float h) {
  const SDL_FRect rect{x, y, w, h};
  SDL_RenderFillRect(r, &rect);
}

Uint8 scaled(Uint8 value, float by) noexcept {
  return static_cast<Uint8>(std::clamp(static_cast<float>(value) * by, 0.0F, 255.0F));
}

constexpr screen::Rect moved(const screen::Rect& r, float dx, float dy) noexcept {
  return {r.x + dx, r.y + dy, r.w, r.h};
}

// How far a pressed face sinks.
constexpr float kSink = 3.0F;

screen::Rect sunk(const screen::Rect& face, bool pressed) noexcept {
  return pressed ? moved(face, kSink, kSink) : face;
}

// The button sprite, tinted, its corners kept square by a nine-part draw:
// 384 pixels to 96 units.
void drawSpriteButton(SDL_Renderer* r, SDL_Texture* sprite, const screen::Rect& face,
                      SDL_Color tint, bool pressed) {
  const float level = pressed ? 0.6F : 1.0F;
  SDL_SetTextureColorMod(sprite, scaled(tint.r, level), scaled(tint.g, level),
                         scaled(tint.b, level));
  const screen::Rect at = sunk(face, pressed);
  const SDL_FRect dst{at.x, at.y, at.w, at.h};
  constexpr float kCorner = 128.0F;
  SDL_RenderTexture9Grid(r, sprite, nullptr, kCorner, kCorner, kCorner, kCorner, 0.25F, &dst);
}

void drawButton(SDL_Renderer* r, SDL_Texture* sprite, const screen::Rect& face, SDL_Color tint,
                bool pressed) {
  if (sprite != nullptr) {
    drawSpriteButton(r, sprite, face, tint, pressed);
  } else {
    drawPlainButton(r, face, tint, pressed);
  }
}

void caption(SDL_Renderer* r, const screen::Rect& face, std::string_view text, float size,
             float top, SDL_Color color, bool pressed) {
  const screen::Rect at = sunk(face, pressed);
  drawText(r, text, at.x + at.w * 0.5F, at.y + top, size, color, TextAlign::Center);
}

// A nudge button: an arrow for the way the ball goes. Where the face has
// room for it at a readable size (the panel's row, on a tablet) the word
// goes under the arrow; the deck's button is the arrow alone, large.
void drawNudge(SDL_Renderer* r, SDL_Texture* sprite, const screen::Rect& face,
               std::string_view arrow, bool pressed, bool withWord) {
  drawButton(r, sprite, face, kViolet, pressed);
  if (withWord) {
    caption(r, face, arrow, 4.0F, 8.0F, kWhite, pressed);
    caption(r, face, "NUDGE", 2.0F, face.h - 24.0F, kWhite, pressed);
  } else {
    const float size = 8.0F;
    caption(r, face, arrow, size, (face.h - 7.0F * size) * 0.5F, kWhite, pressed);
  }
}

void drawCentre(SDL_Renderer* r, SDL_Texture* sprite, const screen::Rect& face,
                const TouchControls::View& view) {
  std::string_view text = "BUMP";
  SDL_Color tint = kCyan;
  switch (view.centreRole) {
    using enum TouchControls::View::Centre;
    case Launch:
      text = "LAUNCH";
      tint = kAmber;
      break;
    case Play:
      text = "PLAY";
      tint = kGreen;
      break;
    case Bump:
      break;
  }
  drawButton(r, sprite, face, tint, view.centre);
  float size = 5.0F;
  while (size > 3.0F && textWidth(text, size) > face.w - 24.0F) {
    size -= 1.0F;
  }
  caption(r, face, text, size, (face.h - 7.0F * size) * 0.5F, kWhite, view.centre);
}

void drawPause(SDL_Renderer* r, SDL_Texture* sprite, const screen::Rect& face, bool pressed) {
  drawButton(r, sprite, face, kPink, pressed);
  // Two upright bars.
  const screen::Rect at = sunk(face, pressed);
  const float barWidth = 12.0F;
  const float barHeight = at.h * 0.44F;
  const float top = at.y + (at.h - barHeight) * 0.5F;
  const float middle = at.x + at.w * 0.5F;
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(r, kWhite.r, kWhite.g, kWhite.b, SDL_ALPHA_OPAQUE);
  fill(r, middle - barWidth - 5.0F, top, barWidth, barHeight);
  fill(r, middle + 5.0F, top, barWidth, barHeight);
}

// A flipper pad: where a thumb rests. An outline that fills when held.
void drawPad(SDL_Renderer* r, const screen::Rect& pad, SDL_Color tint, bool pressed) {
  if (pad.w <= 0.0F || pad.h <= 0.0F) {
    return;
  }
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r, tint.r, tint.g, tint.b, pressed ? 150 : 44);
  fill(r, pad.x, pad.y, pad.w, pad.h);
  SDL_SetRenderDrawColor(r, tint.r, tint.g, tint.b, pressed ? 255 : 150);
  fill(r, pad.x, pad.y, pad.w, 3.0F);
  fill(r, pad.x, pad.y + pad.h - 3.0F, pad.w, 3.0F);
  fill(r, pad.x, pad.y + 3.0F, 3.0F, pad.h - 6.0F);
  fill(r, pad.x + pad.w - 3.0F, pad.y + 3.0F, 3.0F, pad.h - 6.0F);
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

void padCaption(SDL_Renderer* r, const screen::Rect& pad, bool pressed) {
  constexpr float kSize = 3.0F;
  if (pad.h < 7.0F * kSize + 12.0F || pad.w < textWidth("FLIP", kSize) + 12.0F) {
    return;
  }
  drawText(r, "FLIP", pad.x + pad.w * 0.5F, pad.y + (pad.h - 7.0F * kSize) * 0.5F, kSize,
           pressed ? kWhite : SDL_Color{190, 170, 236, 255}, TextAlign::Center);
}

}  // namespace

void drawPlainButton(SDL_Renderer* r, const screen::Rect& face, SDL_Color tint, bool pressed) {
  if (r == nullptr || face.w <= 0.0F || face.h <= 0.0F) {
    return;
  }
  const screen::Rect at = sunk(face, pressed);
  if (!pressed) {
    // A hard drop shadow, down and to the right.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 150);
    fill(r, face.x + 5.0F, face.y + 5.0F, face.w, face.h);
  }
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(r, tint.r, tint.g, tint.b, SDL_ALPHA_OPAQUE);
  fill(r, at.x, at.y, at.w, at.h);
  // The slab inside the rim: dark when up, the colour itself when held.
  const float level = pressed ? 0.55F : 0.22F;
  SDL_SetRenderDrawColor(r, scaled(tint.r, level), scaled(tint.g, level),
                         static_cast<Uint8>(std::min(255, scaled(tint.b, level) + 28)),
                         SDL_ALPHA_OPAQUE);
  fill(r, at.x + 4.0F, at.y + 4.0F, at.w - 8.0F, at.h - 8.0F);
  if (!pressed) {
    // A highlight along the top of the slab.
    SDL_SetRenderDrawColor(r, scaled(tint.r, 0.5F), scaled(tint.g, 0.5F), scaled(tint.b, 0.5F),
                           SDL_ALPHA_OPAQUE);
    fill(r, at.x + 4.0F, at.y + 4.0F, at.w - 8.0F, 4.0F);
  }
}

void drawCaptionButton(SDL_Renderer* r, const screen::Rect& face, std::string_view text,
                       SDL_Color tint, bool pressed, float largest, float smallest) {
  if (r == nullptr) {
    return;
  }
  drawPlainButton(r, face, tint, pressed);
  float size = largest;
  while (size > smallest && textWidth(text, size) > face.w - 24.0F) {
    size -= 1.0F;
  }
  caption(r, face, text, size, (face.h - 7.0F * size) * 0.5F, kWhite, pressed);
}

void dimAround(SDL_Renderer* r, const screen::Layout& layout) {
  if (r == nullptr) {
    return;
  }
  const screen::Rect& field = layout.playfield;
  const float right = field.x + field.w;
  const float bottom = field.y + field.h;
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r, 10, 0, 26, 140);
  fill(r, 0.0F, 0.0F, layout.canvasWidth, field.y);
  fill(r, 0.0F, bottom, layout.canvasWidth, layout.canvasHeight - bottom);
  fill(r, 0.0F, field.y, field.x, field.h);
  fill(r, right, field.y, layout.canvasWidth - right, field.h);
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

void TableRenderer::drawTouchOverlay(SDL_Renderer* r, const screen::Layout& layout,
                                     const TouchControls::View& view) const {
  if (r == nullptr || layout.idiom != screen::Idiom::Touch) {
    return;
  }
  SDL_Texture* const sprite = textures_.button.get();

  if (layout.composition == screen::Composition::Stack) {
    const screen::Rect& bed = layout.deck;
    const SDL_FRect bedRect{bed.x, bed.y, bed.w, bed.h};
    if (SDL_Texture* const art = textures_.deck.get()) {
      // The art is the tallest deck there is: show as much of its top as
      // this deck is high, two pixels to the unit.
      const SDL_FRect src{0.0F, 0.0F, bed.w * 2.0F, bed.h * 2.0F};
      SDL_RenderTexture(r, art, &src, &bedRect);
    } else {
      SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
      SDL_SetRenderDrawColor(r, 26, 10, 58, SDL_ALPHA_OPAQUE);
      SDL_RenderFillRect(r, &bedRect);
      SDL_SetRenderDrawColor(r, kPink.r, kPink.g, kPink.b, SDL_ALPHA_OPAQUE);
      fill(r, bed.x, bed.y, bed.w, 2.0F);
    }
    // The flipper pads: the corner beside the button row, and under the row
    // whatever the deck has left. On a short deck the corner is all of it.
    const float below = bed.h - deck::kPadTop - 8.0F;
    const float half = bed.w * 0.5F;
    screen::Rect leftCorner = moved(deck::kLeftCorner, bed.x, bed.y);
    screen::Rect rightCorner = moved(deck::kRightCorner, bed.x, bed.y);
    // Never taller than the deck itself.
    leftCorner.h = rightCorner.h = std::min(leftCorner.h, bed.h - 20.0F);
    drawPad(r, leftCorner, kCyan, view.left);
    drawPad(r, rightCorner, kPink, view.right);
    if (below >= 24.0F) {
      const screen::Rect leftPad{bed.x + 8.0F, bed.y + deck::kPadTop, half - 12.0F, below};
      const screen::Rect rightPad{bed.x + half + 4.0F, bed.y + deck::kPadTop, half - 12.0F, below};
      drawPad(r, leftPad, kCyan, view.left);
      drawPad(r, rightPad, kPink, view.right);
      padCaption(r, leftPad, view.left);
      padCaption(r, rightPad, view.right);
    } else {
      padCaption(r, leftCorner, view.left);
      padCaption(r, rightCorner, view.right);
    }
    drawNudge(r, sprite, moved(deck::kNudgeLeftButton, bed.x, bed.y), "<", view.nudgeLeft, false);
    drawCentre(r, sprite, moved(deck::kCentreButton, bed.x, bed.y), view);
    drawNudge(r, sprite, moved(deck::kNudgeRightButton, bed.x, bed.y), ">", view.nudgeRight,
              false);
    drawPause(r, sprite, moved(strip::kPauseButton, layout.hud.x, layout.hud.y), view.pause);
    return;
  }

  // Classic: the row at the foot of the panel, the pause button at its head,
  // and a mark for each thumb: the corner of the panel beside the row, and
  // across the table the margin beside its foot, if the window leaves one.
  // Nothing is drawn over the table itself.
  const float dx = layout.playfield.x;
  const float dy = layout.playfield.y;
  // The word under the arrow is at the panel's smallest size: only where
  // that is large enough on this screen.
  const bool words = screen::smallestTextSize(layout) == 2.0F;
  drawNudge(r, sprite, moved(panel::kNudgeLeftButton, dx, dy), "<", view.nudgeLeft, words);
  drawCentre(r, sprite, moved(panel::kCentreButton, dx, dy), view);
  drawNudge(r, sprite, moved(panel::kNudgeRightButton, dx, dy), ">", view.nudgeRight, words);
  drawPause(r, sprite, moved(panel::kPauseButton, dx, dy), view.pause);
  const screen::Rect rightCorner = moved(panel::kFlipperCorner, dx, dy);
  drawPad(r, rightCorner, kPink, view.right);
  padCaption(r, rightCorner, view.right);
  if (dx >= 16.0F) {
    drawPad(r, {4.0F, rightCorner.y, dx - 8.0F, rightCorner.h}, kCyan, view.left);
  }
}

}  // namespace pinball
