#include "platform/TouchOverlay.hpp"

#include <algorithm>
#include <string_view>

#include "core/PanelLayout.hpp"
#include "generated/ArtPalette.hpp"
#include "platform/TableRenderer.hpp"
#include "platform/Text.hpp"

namespace pinball {

namespace {

// The art's tints (tools/art/palette.py): one colour per kind of control.
constexpr SDL_Color sdl(art::Rgb c) noexcept { return {c.r, c.g, c.b, 255}; }
constexpr SDL_Color kCentreTint = sdl(art::kTintButtonCentre);
constexpr SDL_Color kNudgeTint = sdl(art::kTintButtonNudge);
constexpr SDL_Color kPauseTint = sdl(art::kTintButtonPause);
constexpr SDL_Color kLaunchTint = sdl(art::kTintButtonLaunch);
constexpr SDL_Color kPlayTint = sdl(art::kGreen);
constexpr SDL_Color kPadTint = sdl(art::kTintPad);
constexpr SDL_Color kRightPadTint = sdl(art::kPink);

void fill(SDL_Renderer* r, float x, float y, float w, float h) {
  const SDL_FRect rect{x, y, w, h};
  SDL_RenderFillRect(r, &rect);
}

Uint8 scaled(Uint8 value, float by) noexcept {
  return static_cast<Uint8>(std::clamp(static_cast<float>(value) * by, 0.0F, 255.0F));
}

constexpr Uint8 mixed(Uint8 a, Uint8 b, float t) noexcept {
  return static_cast<Uint8>(static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t +
                            0.5F);
}

constexpr screen::Rect moved(const screen::Rect& r, float dx, float dy) noexcept {
  return {r.x + dx, r.y + dy, r.w, r.h};
}

// How far a pressed face sinks.
constexpr float kSink = 3.0F;

screen::Rect sunk(const screen::Rect& face, bool pressed) noexcept {
  return pressed ? moved(face, kSink, kSink) : face;
}

// The button sprite, tinted, its corners kept square by a nine-part draw
// (384 pixels to 96 units: 128-pixel corners at a quarter are 32 units),
// then its lights untinted over it. Never larger corners than the face has
// room for.
void drawSpriteButton(SDL_Renderer* r, const ButtonArt& art, const screen::Rect& face,
                      SDL_Color tint, bool pressed) {
  // Held down: sunk and darker, still light enough for its ink caption.
  const float level = pressed ? 0.8F : 1.0F;
  SDL_SetTextureColorMod(art.face, scaled(tint.r, level), scaled(tint.g, level),
                         scaled(tint.b, level));
  const screen::Rect at = sunk(face, pressed);
  const SDL_FRect dst{at.x, at.y, at.w, at.h};
  constexpr float kCorner = 128.0F;
  const float scale = std::min({0.25F, at.w * 0.5F / kCorner, at.h * 0.5F / kCorner});
  SDL_RenderTexture9Grid(r, art.face, nullptr, kCorner, kCorner, kCorner, kCorner, scale, &dst);
  if (art.gloss != nullptr) {
    SDL_SetTextureAlphaMod(art.gloss, pressed ? 150 : 255);
    SDL_RenderTexture9Grid(r, art.gloss, nullptr, kCorner, kCorner, kCorner, kCorner, scale, &dst);
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
void drawNudge(SDL_Renderer* r, const ButtonArt& art, const screen::Rect& face,
               std::string_view arrow, bool pressed, bool withWord) {
  drawButtonFace(r, art, face, kNudgeTint, pressed);
  const SDL_Color ink = captionColor(art);
  if (withWord) {
    caption(r, face, arrow, 4.0F, 8.0F, ink, pressed);
    caption(r, face, "NUDGE", 2.0F, face.h - 24.0F, ink, pressed);
  } else {
    const float size = 8.0F;
    caption(r, face, arrow, size, (face.h - 7.0F * size) * 0.5F, ink, pressed);
  }
}

void drawCentre(SDL_Renderer* r, const ButtonArt& art, const screen::Rect& face,
                const TouchControls::View& view) {
  std::string_view text = "BUMP";
  SDL_Color tint = kCentreTint;
  switch (view.centreRole) {
    using enum TouchControls::View::Centre;
    case Launch:
      text = "LAUNCH";
      tint = kLaunchTint;
      break;
    case Play:
      text = "PLAY";
      tint = kPlayTint;
      break;
    case Bump:
      break;
  }
  drawButtonFace(r, art, face, tint, view.centre);
  float size = 5.0F;
  while (size > 3.0F && textWidth(text, size) > face.w - 24.0F) {
    size -= 1.0F;
  }
  caption(r, face, text, size, (face.h - 7.0F * size) * 0.5F, captionColor(art), view.centre);
}

void drawPause(SDL_Renderer* r, const ButtonArt& art, const screen::Rect& face, bool pressed) {
  drawButtonFace(r, art, face, kPauseTint, pressed);
  // Two upright bars.
  const screen::Rect at = sunk(face, pressed);
  const float barWidth = 12.0F;
  const float barHeight = at.h * 0.44F;
  const float top = at.y + (at.h - barHeight) * 0.5F;
  const float middle = at.x + at.w * 0.5F;
  const SDL_Color ink = captionColor(art);
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(r, ink.r, ink.g, ink.b, SDL_ALPHA_OPAQUE);
  fill(r, middle - barWidth - 5.0F, top, barWidth, barHeight);
  fill(r, middle + 5.0F, top, barWidth, barHeight);
}

// A flipper pad: where a thumb rests. With art, a key like the others; else
// an outline that fills when held.
void drawPad(SDL_Renderer* r, const ButtonArt& art, const screen::Rect& pad, SDL_Color tint,
             bool pressed) {
  if (pad.w <= 0.0F || pad.h <= 0.0F) {
    return;
  }
  if (art.face != nullptr) {
    drawButtonFace(r, art, pad, tint, pressed);
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

void padCaption(SDL_Renderer* r, const ButtonArt& art, const screen::Rect& pad, bool pressed,
                float size = 3.0F) {
  if (pad.h < 7.0F * size + 12.0F || pad.w < textWidth("FLIP", size) + 12.0F) {
    return;
  }
  const SDL_Color color = art.face != nullptr ? kCaptionOnArt
                          : pressed           ? kCaptionPlain
                                              : SDL_Color{190, 170, 236, 255};
  const screen::Rect at = art.face != nullptr ? sunk(pad, pressed) : pad;
  drawText(r, "FLIP", at.x + at.w * 0.5F, at.y + (at.h - 7.0F * size) * 0.5F, size, color,
           TextAlign::Center);
}

// The large pads under the button row on a tall deck, from the art: the
// pad tint taken part of the way to the backdrop so the row above stays the
// brighter thing. Only where they come to a useful height.
constexpr float kArtPadMinHeight = 60.0F;
constexpr float kArtPadCaption = 6.0F;
constexpr SDL_Color padTintBelow(SDL_Color tint) noexcept {
  constexpr float kTowardBackdrop = 0.45F;
  return {mixed(tint.r, art::kBackdrop.r, kTowardBackdrop),
          mixed(tint.g, art::kBackdrop.g, kTowardBackdrop),
          mixed(tint.b, art::kBackdrop.b, kTowardBackdrop), 255};
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

void drawButtonFace(SDL_Renderer* r, const ButtonArt& art, const screen::Rect& face,
                    SDL_Color tint, bool pressed) {
  if (r == nullptr || face.w <= 0.0F || face.h <= 0.0F) {
    return;
  }
  if (art.face != nullptr) {
    drawSpriteButton(r, art, face, tint, pressed);
  } else {
    drawPlainButton(r, face, tint, pressed);
  }
}

void drawCaptionButton(SDL_Renderer* r, const screen::Rect& face, std::string_view text,
                       SDL_Color tint, bool pressed, float largest, float smallest,
                       const ButtonArt& art) {
  if (r == nullptr) {
    return;
  }
  drawButtonFace(r, art, face, tint, pressed);
  float size = largest;
  while (size > smallest && textWidth(text, size) > face.w - 24.0F) {
    size -= 1.0F;
  }
  caption(r, face, text, size, (face.h - 7.0F * size) * 0.5F, captionColor(art), pressed);
}

void drawDialogCard(SDL_Renderer* r, const OverlayArt& art, const SDL_FRect& box) {
  if (r == nullptr) {
    return;
  }
  if (art.dialog == nullptr) {
    drawNeonFrame(r, box);
    return;
  }
  // 384 pixels to 192 units: 96-pixel corners at a half are 48 units.
  constexpr float kCorner = 96.0F;
  const float scale = std::min({0.5F, box.w * 0.5F / kCorner, box.h * 0.5F / kCorner});
  SDL_RenderTexture9Grid(r, art.dialog, nullptr, kCorner, kCorner, kCorner, kCorner, scale, &box);
}

void drawDialogRule(SDL_Renderer* r, const OverlayArt& art, float centerX, float y,
                    float halfWidth) {
  if (r == nullptr) {
    return;
  }
  if (art.dialog == nullptr) {
    drawNeonRule(r, centerX, y, halfWidth);
    return;
  }
  // As the panel's trim: an ink bed, the colour, a hard highlight on top.
  const float left = centerX - halfWidth;
  const float width = halfWidth * 2.0F;
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(r, art::kInk.r, art::kInk.g, art::kInk.b, SDL_ALPHA_OPAQUE);
  fill(r, left, y - 2.0F, width, 6.0F);
  SDL_SetRenderDrawColor(r, art::kTeal.r, art::kTeal.g, art::kTeal.b, SDL_ALPHA_OPAQUE);
  fill(r, left, y - 1.0F, width, 3.0F);
  SDL_SetRenderDrawColor(r, art::kAqua.r, art::kAqua.g, art::kAqua.b, SDL_ALPHA_OPAQUE);
  fill(r, left, y - 1.0F, width, 1.0F);
}

bool drawTitleArt(SDL_Renderer* r, SDL_Texture* title, const screen::Rect& area) {
  if (r == nullptr || title == nullptr || area.w <= 0.0F || area.h <= 0.0F) {
    return false;
  }
  constexpr float kWidth = 480.0F;
  constexpr float kHeight = 120.0F;
  const float scale = std::min({1.0F, area.w / kWidth, area.h / kHeight});
  const SDL_FRect dst{area.x + (area.w - kWidth * scale) * 0.5F,
                      area.y + (area.h - kHeight * scale) * 0.5F, kWidth * scale,
                      kHeight * scale};
  SDL_RenderTexture(r, title, nullptr, &dst);
  return true;
}

void dimAround(SDL_Renderer* r, const screen::Layout& layout) {
  if (r == nullptr) {
    return;
  }
  const screen::Rect& field = layout.playfield;
  const float right = field.x + field.w;
  const float bottom = field.y + field.h;
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r, screen::kBackdropRgb[0], screen::kBackdropRgb[1],
                         screen::kBackdropRgb[2], 140);
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
  const ButtonArt art = overlayArt().button;
  const bool withArt = art.face != nullptr;
  const SDL_Color leftTint = kPadTint;
  const SDL_Color rightTint = withArt ? kPadTint : kRightPadTint;

  if (layout.composition == screen::Composition::Stack) {
    const screen::Rect& bed = layout.deck;
    const SDL_FRect bedRect{bed.x, bed.y, bed.w, bed.h};
    if (SDL_Texture* const deckArt = textures_.deck.get()) {
      // The art is the tallest deck there is: show as much of its top as
      // this deck is high, two pixels to the unit.
      const SDL_FRect src{0.0F, 0.0F, bed.w * 2.0F, bed.h * 2.0F};
      SDL_RenderTexture(r, deckArt, &src, &bedRect);
    } else {
      SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
      SDL_SetRenderDrawColor(r, art::kInk.r, art::kInk.g, art::kInk.b, SDL_ALPHA_OPAQUE);
      SDL_RenderFillRect(r, &bedRect);
      SDL_SetRenderDrawColor(r, kRightPadTint.r, kRightPadTint.g, kRightPadTint.b,
                             SDL_ALPHA_OPAQUE);
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
    drawPad(r, art, leftCorner, leftTint, view.left);
    drawPad(r, art, rightCorner, rightTint, view.right);
    padCaption(r, art, leftCorner, view.left);
    padCaption(r, art, rightCorner, view.right);
    if (withArt) {
      // Two large keys from 6 units under the row's bed to 8 above the
      // deck's foot, each half the deck less the margins.
      const float top = deck::kPadTop + 6.0F;
      const float height = bed.h - top - 8.0F;
      if (height >= kArtPadMinHeight) {
        const screen::Rect leftPad{bed.x + 8.0F, bed.y + top, half - 12.0F, height};
        const screen::Rect rightPad{bed.x + half + 4.0F, bed.y + top, half - 12.0F, height};
        drawPad(r, art, leftPad, padTintBelow(leftTint), view.left);
        drawPad(r, art, rightPad, padTintBelow(rightTint), view.right);
        padCaption(r, art, leftPad, view.left, kArtPadCaption);
        padCaption(r, art, rightPad, view.right, kArtPadCaption);
      }
    } else if (below >= 24.0F) {
      const screen::Rect leftPad{bed.x + 8.0F, bed.y + deck::kPadTop, half - 12.0F, below};
      const screen::Rect rightPad{bed.x + half + 4.0F, bed.y + deck::kPadTop, half - 12.0F, below};
      drawPad(r, art, leftPad, leftTint, view.left);
      drawPad(r, art, rightPad, rightTint, view.right);
      padCaption(r, art, leftPad, view.left);
      padCaption(r, art, rightPad, view.right);
    }
    drawNudge(r, art, moved(deck::kNudgeLeftButton, bed.x, bed.y), "<", view.nudgeLeft, false);
    drawCentre(r, art, moved(deck::kCentreButton, bed.x, bed.y), view);
    drawNudge(r, art, moved(deck::kNudgeRightButton, bed.x, bed.y), ">", view.nudgeRight, false);
    drawPause(r, art, moved(strip::kPauseButton, layout.hud.x, layout.hud.y), view.pause);
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
  drawNudge(r, art, moved(panel::kNudgeLeftButton, dx, dy), "<", view.nudgeLeft, words);
  drawCentre(r, art, moved(panel::kCentreButton, dx, dy), view);
  drawNudge(r, art, moved(panel::kNudgeRightButton, dx, dy), ">", view.nudgeRight, words);
  drawPause(r, art, moved(panel::kPauseButton, dx, dy), view.pause);
  const screen::Rect rightCorner = moved(panel::kFlipperCorner, dx, dy);
  drawPad(r, art, rightCorner, rightTint, view.right);
  padCaption(r, art, rightCorner, view.right);
  if (dx >= 16.0F) {
    // A strip of the margin, too narrow for a key: always the outline.
    drawPad(r, ButtonArt{}, {4.0F, rightCorner.y, dx - 8.0F, rightCorner.h}, leftTint, view.left);
  }
}

}  // namespace pinball
