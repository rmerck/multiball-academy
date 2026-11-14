#pragma once

#include <SDL3/SDL.h>

#include <string_view>

#include "core/ScreenLayout.hpp"
#include "generated/ArtPalette.hpp"

// The on-screen controls of the touch idiom: the buttons and the flipper
// pads, drawn from the rectangles in screen::Layout. They are drawn by
// TableRenderer::drawTouchOverlay (platform/TableRenderer.hpp), which owns
// the textures. The plain button the dialogs share is declared here.
namespace pinball {

// The button art: a neutral glossy key (sprites/button.png) that takes the
// tint, and its lights (sprites/button_gloss.png) drawn untinted over it.
// Both nine-part, 32-unit corners. Null where the build has no such art.
struct ButtonArt {
  SDL_Texture* face = nullptr;
  SDL_Texture* gloss = nullptr;
};

// Art the dialogs share with the table: the card behind them (ui/dialog.png,
// nine-part, 48-unit corners), the title lockup (ui/title.png, 480 x 120
// units) and the buttons. Any of it may be null: plain drawing then.
struct OverlayArt {
  SDL_Texture* dialog = nullptr;
  SDL_Texture* title = nullptr;
  ButtonArt button;
  // The title screen's: one lamp of its dot display (sprites/dot.png, 8
  // units, tinted) and the soft light (sprites/glow.png, additive). The
  // glow belongs to the table and must be left additive.
  SDL_Texture* dot = nullptr;
  SDL_Texture* glow = nullptr;
};

// Captions, arrows and marks on a button: light on the plain dark slab,
// ink on the bright glossy key. Nothing else on the screen is drawn in
// exactly these colours, so the layout check can find a caption by them.
inline constexpr SDL_Color kCaptionPlain{248, 240, 255, 255};
inline constexpr SDL_Color kCaptionOnArt{12, 10, 38, 255};
[[nodiscard]] constexpr SDL_Color captionColor(const ButtonArt& art) noexcept {
  return art.face != nullptr ? kCaptionOnArt : kCaptionPlain;
}
// The tints for keys that take ink captions: every face light enough for
// 3:1 against kCaptionOnArt, held down too (tools/art/palette.py). Off: a
// setting that is off, or a key that leads nowhere.
inline constexpr SDL_Color kButtonOrchid{art::kTintButtonNudge.r, art::kTintButtonNudge.g,
                                         art::kTintButtonNudge.b, 255};
inline constexpr SDL_Color kButtonOff{art::kTintButtonOff.r, art::kTintButtonOff.g,
                                      art::kTintButtonOff.b, 255};

// A button face drawn without art: a chunky bordered slab in the given
// colour with a drop shadow, pushed in and filled when pressed.
void drawPlainButton(SDL_Renderer* renderer, const screen::Rect& face, SDL_Color tint,
                     bool pressed);

// The button face from the art if there is any (tinted, darker and sunk
// when pressed), else drawPlainButton.
void drawButtonFace(SDL_Renderer* renderer, const ButtonArt& art, const screen::Rect& face,
                    SDL_Color tint, bool pressed);

// drawButtonFace with a caption centred on it, at the largest whole text
// size from `largest` down to `smallest` that fits the face. Every caption
// there is fits at 3.
void drawCaptionButton(SDL_Renderer* renderer, const screen::Rect& face, std::string_view caption,
                       SDL_Color tint, bool pressed, float largest = 4.0F,
                       float smallest = 3.0F, const ButtonArt& art = {});

// The card behind a dialog: the art if there is any, else the neon frame.
void drawDialogCard(SDL_Renderer* renderer, const OverlayArt& art, const SDL_FRect& box);

// A rule across a dialog, centred on x: a glossy teal bar on the card of
// art, else the neon rule.
void drawDialogRule(SDL_Renderer* renderer, const OverlayArt& art, float centerX, float y,
                    float halfWidth);

// The title lockup centred in area, as large as fits (never above its own
// size). False, drawing nothing, if there is no title art.
bool drawTitleArt(SDL_Renderer* renderer, SDL_Texture* title, const screen::Rect& area);

// The same dimming the paused table gets, over everything on the canvas but
// the table: the status display, the deck and the backdrop.
void dimAround(SDL_Renderer* renderer, const screen::Layout& layout);

}  // namespace pinball
