// Development tool: runs the game headless with scripted input and writes one
// rendered frame to a PNG in the current directory. No window is opened.
// Used to review art and layout on any window shape, and by ctest as a
// rendering smoke test.
//
// Usage: pinball_snapshot <steps> <name.png> [state] [options]
//
// state    play (the default), launch, pause, interrupted, confirm, about,
//          about-info, licences, initials, gameover, course, multiball
// options  --layout <preset | WxH[@scale][+top,bottom,left,right]>
//          --surface WxH  --insets top,bottom,left,right
//          --idiom desktop|touch
//          --page N            which licence page, from 1
//          --message TEXT      shown in the message window
//          --pressed LIST      of left,right,centre,nl,nr,pause
//          --zones             outline the safe area, zones and hit rectangles
//
// Sizes and insets are in points. Without a scale the picture is the
// layout's canvas, one pixel to the unit; with one it is the window at that
// many pixels to the point, as a device would show it.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/Args.hpp"
#include "core/DisplayText.hpp"
#include "core/Game.hpp"
#include "core/HighScores.hpp"
#include "core/InitialsEntry.hpp"
#include "core/OverlayLayout.hpp"
#include "core/ScreenLayout.hpp"
#include "core/SurfaceSpec.hpp"
#include "core/TouchControls.hpp"
#include "platform/About.hpp"
#include "platform/Assets.hpp"
#include "platform/GameViews.hpp"
#include "platform/InitialsScreen.hpp"
#include "platform/PauseMenu.hpp"
#include "platform/SafeFile.hpp"
#include "platform/TableRenderer.hpp"

namespace {

struct SurfaceDeleter {
  void operator()(SDL_Surface* s) const noexcept { SDL_DestroySurface(s); }
};
struct RendererDeleter {
  void operator()(SDL_Renderer* r) const noexcept { SDL_DestroyRenderer(r); }
};
struct IoCloser {
  void operator()(SDL_IOStream* io) const noexcept { SDL_CloseIO(io); }
};

// The output is always a plain file name in the current directory: lower-case
// letters, digits and underscores, ending in ".png". No path separators.
bool isPlainPngName(std::string_view name) noexcept {
  constexpr std::string_view kExtension = ".png";
  if (name.size() <= kExtension.size() || name.size() > 64 || !name.ends_with(kExtension)) {
    return false;
  }
  return std::ranges::all_of(name.substr(0, name.size() - kExtension.size()), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
  });
}

// A fixed script that exercises the table: launch, then work the flippers.
pinball::InputState scriptedInput(std::uint32_t step) noexcept {
  pinball::InputState input;
  input.plunger = (step % 1200U) >= 20U && (step % 1200U) < 140U;
  input.leftFlipper = (step / 29U) % 2U == 0U;
  input.rightFlipper = (step / 41U) % 2U == 0U;
  return input;
}

// The same launches with the flippers left alone, so every ball drains.
pinball::InputState losingInput(std::uint32_t step) noexcept {
  pinball::InputState input;
  input.plunger = (step % 1200U) >= 20U && (step % 1200U) < 140U;
  return input;
}

// True if the playfield part of the frame holds a real picture: many
// different colours, not a flat fill or a few lines. The rest is ignored,
// because the panel is filled even when nothing else draws.
bool hasContent(SDL_Surface* surface, const pinball::screen::Rect& playfield) {
  constexpr std::size_t kNeeded = 16;
  if (!SDL_LockSurface(surface)) {
    return false;
  }
  std::array<std::uint32_t, kNeeded> seen{};
  std::size_t distinct = 0;
  const auto* pixels = static_cast<const std::uint8_t*>(surface->pixels);
  // The playfield's part of the surface, kept inside it.
  const int left = std::clamp(static_cast<int>(playfield.x), 0, surface->w);
  const int top = std::clamp(static_cast<int>(playfield.y), 0, surface->h);
  const int right = std::clamp(static_cast<int>(playfield.x + playfield.w), left, surface->w);
  const int bottom = std::clamp(static_cast<int>(playfield.y + playfield.h), top, surface->h);
  // Sample every fourth pixel of every fourth row: plenty, and quick.
  for (int y = top; y < bottom && distinct < kNeeded; y += 4) {
    const std::uint8_t* row =
        pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->pitch);
    for (int x = left; x < right && distinct < kNeeded; x += 4) {
      std::uint32_t value = 0;
      std::memcpy(&value, row + static_cast<std::size_t>(x) * 4U, sizeof value);
      const auto known = std::span(seen).first(distinct);
      if (std::ranges::find(known, value) == known.end()) {
        seen[distinct++] = value;
      }
    }
  }
  SDL_UnlockSurface(surface);
  return distinct >= kNeeded;
}

enum class State : std::uint8_t {
  Play,
  Launch,
  Pause,
  Interrupted,
  Confirm,
  About,
  AboutInfo,
  Licences,
  Initials,
  GameOver,
  Course,
  Multiball,
};

struct StateName {
  std::string_view name;
  State state;
};
constexpr std::array<StateName, 12> kStates{{
    {"play", State::Play},
    {"launch", State::Launch},
    {"pause", State::Pause},
    {"interrupted", State::Interrupted},
    {"confirm", State::Confirm},
    {"about", State::About},
    {"about-info", State::AboutInfo},
    {"licences", State::Licences},
    {"initials", State::Initials},
    {"gameover", State::GameOver},
    {"course", State::Course},
    {"multiball", State::Multiball},
}};

// What the command line asked for.
struct Request {
  std::uint32_t steps = 0;
  std::string output;
  State state = State::Play;
  pinball::screen::Surface surface{};
  pinball::screen::Idiom idiom = pinball::screen::Idiom::Desktop;
  int scale = 0;  // Pixels per point; 0 for one pixel per canvas unit.
  std::size_t licencePage = 0;
  std::string_view message;
  pinball::TouchControls::View pressed{};
  bool zones = false;
};

// The largest picture drawn, on either side, in canvas units and in pixels.
constexpr float kMaxPictureSide = 8192.0F;

constexpr const char* kUsage =
    "Usage: pinball_snapshot <steps 1..1000000> <name.png> [state] [options]\n"
    "  state: play launch pause interrupted confirm about about-info licences\n"
    "         initials gameover course multiball\n"
    "  --layout <preset | WxH[@scale][+top,bottom,left,right]>   points, 100..4096; scale 1..4\n"
    "  --surface WxH   --insets top,bottom,left,right\n"
    "  --idiom desktop|touch\n"
    "  --page <1..99>   --message <TEXT>   --zones\n"
    "  --pressed <left,right,centre,nl,nr,pause>\n";

// "left,right,..." into the view. False if a word is unknown or repeated.
bool parsePressed(std::string_view text, pinball::TouchControls::View& view) noexcept {
  struct Word {
    std::string_view name;
    bool pinball::TouchControls::View::* flag;
  };
  using View = pinball::TouchControls::View;
  constexpr std::array<Word, 6> kWords{{{"left", &View::left},
                                        {"right", &View::right},
                                        {"centre", &View::centre},
                                        {"nl", &View::nudgeLeft},
                                        {"nr", &View::nudgeRight},
                                        {"pause", &View::pause}}};
  while (true) {
    const std::size_t comma = text.find(',');
    const std::string_view word = text.substr(0, comma);
    const auto found = std::ranges::find(kWords, word, &Word::name);
    if (found == kWords.end() || view.*(found->flag)) {
      return false;
    }
    view.*(found->flag) = true;
    if (comma == std::string_view::npos) {
      return true;
    }
    text.remove_prefix(comma + 1);
  }
}

// Reads the whole command line. Nothing is guessed: an unknown word, a
// repeated or half-given option, or a number out of range is refused.
std::optional<Request> parseArguments(std::span<char* const> args) {
  namespace screen = pinball::screen;
  if (args.size() < 3) {
    return std::nullopt;
  }
  Request request;
  const auto steps = pinball::parseFrameLimit(args[1]);
  const std::string_view name = args[2];
  if (!steps || !isPlainPngName(name)) {
    return std::nullopt;
  }
  request.steps = *steps;
  request.output = std::string(name);

  std::size_t next = 3;
  if (next < args.size() && !std::string_view(args[next]).starts_with("--")) {
    const auto found = std::ranges::find(kStates, std::string_view(args[next]), &StateName::name);
    if (found == kStates.end()) {
      return std::nullopt;
    }
    request.state = found->state;
    ++next;
  }

  std::optional<screen::SurfaceSpec> layout;
  std::optional<screen::SurfaceSpec> surface;
  std::optional<std::string_view> insets;
  std::optional<screen::Idiom> idiom;
  bool page = false;
  bool message = false;
  bool pressed = false;
  for (; next < args.size(); ++next) {
    const std::string_view option = args[next];
    if (option == "--zones") {
      if (request.zones) {
        return std::nullopt;
      }
      request.zones = true;
      continue;
    }
    // Every other option takes one value.
    if (next + 1 >= args.size()) {
      return std::nullopt;
    }
    const std::string_view value = args[++next];
    if (option == "--layout" && !layout) {
      layout = screen::parseSurfaceSpec(value, screen::presets());
      if (!layout) {
        return std::nullopt;
      }
    } else if (option == "--surface" && !surface) {
      // A bare size: no preset, scale or insets here.
      surface = screen::parseSurfaceSpec(value, {});
      if (!surface || value.find_first_of("@+") != std::string_view::npos) {
        return std::nullopt;
      }
    } else if (option == "--insets" && !insets) {
      insets = value;
    } else if (option == "--idiom" && !idiom) {
      if (value == "desktop") {
        idiom = screen::Idiom::Desktop;
      } else if (value == "touch") {
        idiom = screen::Idiom::Touch;
      } else {
        return std::nullopt;
      }
    } else if (option == "--page" && !page) {
      const auto number = screen::parseSpecNumber(value, 1, 99);
      if (!number) {
        return std::nullopt;
      }
      request.licencePage = *number - 1;
      page = true;
    } else if (option == "--message" && !message) {
      // Only what the rules themselves could say.
      if (!pinball::display::fits(value, pinball::display::kMaxMessageLength)) {
        return std::nullopt;
      }
      request.message = value;
      message = true;
    } else if (option == "--pressed" && !pressed) {
      if (!parsePressed(value, request.pressed)) {
        return std::nullopt;
      }
      pressed = true;
    } else {
      return std::nullopt;
    }
  }
  // One way of naming the window at a time, and insets only with a size.
  if ((layout && surface) || (insets && !surface)) {
    return std::nullopt;
  }
  if (surface) {
    if (insets && !screen::parseSpecInsets(*insets, surface->surface)) {
      return std::nullopt;
    }
    layout = surface;
  }
  if (layout) {
    request.surface = layout->surface;
    request.idiom = layout->idiom;
    request.scale = layout->scale;
  } else {
    const screen::Preset* const desktop = screen::findPreset("desktop");
    if (desktop == nullptr) {
      return std::nullopt;
    }
    request.surface = desktop->surface;
    request.idiom = desktop->idiom;
  }
  if (idiom) {
    request.idiom = *idiom;
  }
  return request;
}

void outline(SDL_Renderer* r, const pinball::screen::Rect& rect, SDL_Color color) {
  if (rect.w <= 0.0F || rect.h <= 0.0F) {
    return;
  }
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r, color.r, color.g, color.b, 40);
  const SDL_FRect area{rect.x, rect.y, rect.w, rect.h};
  SDL_RenderFillRect(r, &area);
  SDL_SetRenderDrawColor(r, color.r, color.g, color.b, 255);
  for (int i = 0; i < 2; ++i) {
    const auto inset = static_cast<float>(i);
    const SDL_FRect line{rect.x + inset, rect.y + inset, rect.w - 2.0F * inset,
                         rect.h - 2.0F * inset};
    SDL_RenderRect(r, &line);
  }
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

// The geometry a touch lands in, over the picture.
void drawZones(SDL_Renderer* r, const pinball::screen::Layout& layout) {
  outline(r, layout.flipperLeftZone, {70, 235, 255, 255});
  outline(r, layout.flipperRightZone, {255, 60, 170, 255});
  outline(r, layout.safe, {255, 255, 255, 255});
  for (const pinball::screen::Rect& hit :
       {layout.pauseButton, layout.nudgeLeftButton, layout.centreButton, layout.nudgeRightButton}) {
    outline(r, hit, {255, 225, 70, 255});
  }
}

// With a sample table, so the layout of a full list can be checked.
pinball::HighScores sampleScores() {
  pinball::HighScores sample;
  sample.submit({12'345'678, 8, {'A', 'C', 'E'}});
  sample.submit({3'250'000, 4, {'S', 'K', 'Y'}});
  sample.submit({987'500, 2, {'J', 'E', 'T'}});
  sample.submit({400'000, 1});
  sample.submit({182'500, 0, {'W', 'M', 'W'}});
  return sample;
}

int run(const Request& request) {
  namespace screen = pinball::screen;
  const screen::Layout layout = screen::computeLayout(request.surface, request.idiom);
  const bool touch = layout.idiom == screen::Idiom::Touch;
  if (layout.canvasWidth > kMaxPictureSide || layout.canvasHeight > kMaxPictureSide) {
    std::fputs("that window needs a larger canvas than this tool draws\n", stderr);
    return EXIT_FAILURE;
  }

  pinball::AssetLoader assets = pinball::AssetLoader::fromExecutableLocation();
  if (!assets.verifyAll()) {
    std::fputs("asset integrity check failed\n", stderr);
    return EXIT_FAILURE;
  }

  // The frame is the layout's canvas, unit for pixel.
  const std::unique_ptr<SDL_Surface, SurfaceDeleter> surface(
      SDL_CreateSurface(static_cast<int>(std::lround(layout.canvasWidth)),
                        static_cast<int>(std::lround(layout.canvasHeight)),
                        SDL_PIXELFORMAT_RGBA32));
  if (!surface) {
    std::fprintf(stderr, "SDL_CreateSurface failed: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  {
    // Declared after the surface so it is destroyed first.
    const std::unique_ptr<SDL_Renderer, RendererDeleter> renderer(
        SDL_CreateSoftwareRenderer(surface.get()));
    if (!renderer) {
      std::fprintf(stderr, "SDL_CreateSoftwareRenderer failed: %s\n", SDL_GetError());
      return EXIT_FAILURE;
    }
    SDL_Renderer* const r = renderer.get();
    const std::unique_ptr<pinball::TableRenderer> table = pinball::TableRenderer::create(
        r, assets, touch ? pinball::TextureSet::All : pinball::TextureSet::Desktop);
    if (!table) {
      return EXIT_FAILURE;
    }

    pinball::Game game;
    std::uint32_t step = 0;
    if (request.state == State::GameOver) {
      // Lose every ball first; the steps asked for come after that.
      constexpr std::uint32_t kGiveUp = 400'000;
      while (!game.gameOver() && step < kGiveUp) {
        game.step(losingInput(step++));
      }
      if (!game.gameOver()) {
        std::fputs("the scripted game did not end\n", stderr);
        return EXIT_FAILURE;
      }
    }
    const bool idle = request.state == State::GameOver || request.state == State::Launch;
    for (std::uint32_t i = 0; i < request.steps; ++i) {
      game.step(idle ? pinball::InputState{} : scriptedInput(step++));
    }

    // What a scripted game cannot be steered into is shown by asking.
    pinball::HudPreview preview;
    preview.message = request.message;
    preview.course = request.state == State::Course;
    // Two more balls: one low on the table, one riding the ramp.
    constexpr std::array<pinball::BallView, 2> kMoreBalls{{
        {.position = {250.0F, 640.0F}},
        {.position = {120.0F, 330.0F}, .scale = 1.3F},
    }};
    pinball::DrawOptions options;
    if (request.state == State::Multiball) {
      preview.multiball = pinball::MultiballView{.known = true,
                                                 .ballsLive = 3,
                                                 .locksHeld = 0,
                                                 .lockLit = false,
                                                 .running = true,
                                                 .jackpotLit = true,
                                                 .superJackpotLit = false};
      options.extraBalls = kMoreBalls;
    }
    const bool paused = request.state == State::Pause || request.state == State::Interrupted ||
                        request.state == State::Confirm;
    options.dimmed = paused;

    SDL_SetRenderDrawColor(r, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(r);
    // Alpha 1: the latest step exactly, so the picture depends only on the
    // step count.
    table->drawBackdrop(r, layout);
    table->draw(r, game, 1.0F, layout, options,
                pinball::HudExtras{.bestScore = 0, .newHighScore = false, .preview = &preview});
    pinball::TouchControls::View view = request.pressed;
    view.centreRole = pinball::TouchControls().view(pinball::touchContext(game)).centreRole;
    table->drawTouchOverlay(r, layout, view);
    if (request.zones) {
      drawZones(r, layout);
    }

    switch (request.state) {
      case State::Pause:
      case State::Interrupted:
      case State::Confirm:
        pinball::drawPauseMenu(
            r, layout,
            pinball::MenuState{.muted = false,
                               .musicOn = true,
                               .confirmNewGame = request.state == State::Confirm,
                               .afterInterruption = request.state == State::Interrupted});
        break;
      case State::About:
      case State::AboutInfo:
      case State::Licences: {
        const pinball::AboutPage page = request.state == State::About
                                            ? pinball::AboutPage::Scores
                                            : request.state == State::AboutInfo
                                                  ? pinball::AboutPage::About
                                                  : pinball::AboutPage::Licences;
        pinball::drawAbout(r, sampleScores(), layout,
                           pinball::AboutState{.page = page, .licencePage = request.licencePage});
        break;
      }
      case State::Initials: {
        pinball::InitialsEntry entry(pinball::Initials{'W', 'M', 'A'});
        entry.right();
        pinball::drawInitialsEntry(r, entry, 12'345'678, true, layout);
        break;
      }
      case State::Play:
      case State::Launch:
      case State::GameOver:
      case State::Course:
      case State::Multiball:
        break;
    }
    SDL_RenderPresent(r);
  }

  if (!hasContent(surface.get(), layout.playfield)) {
    std::fputs("rendered frame is blank\n", stderr);
    return EXIT_FAILURE;
  }
  // At a device's scale the picture is the window, that many pixels to the
  // point.
  std::unique_ptr<SDL_Surface, SurfaceDeleter> scaled;
  SDL_Surface* picture = surface.get();
  if (request.scale > 0) {
    const float width = request.surface.width * static_cast<float>(request.scale);
    const float height = request.surface.height * static_cast<float>(request.scale);
    if (width > kMaxPictureSide || height > kMaxPictureSide) {
      std::fputs("that window is too large to draw at that scale\n", stderr);
      return EXIT_FAILURE;
    }
    scaled.reset(SDL_CreateSurface(static_cast<int>(std::lround(width)),
                                   static_cast<int>(std::lround(height)),
                                   SDL_PIXELFORMAT_RGBA32));
    if (!scaled || !SDL_BlitSurfaceScaled(surface.get(), nullptr, scaled.get(), nullptr,
                                          SDL_SCALEMODE_LINEAR)) {
      std::fprintf(stderr, "scaling the picture failed: %s\n", SDL_GetError());
      return EXIT_FAILURE;
    }
    picture = scaled.get();
  }
  // Never write through whatever already has this name, be it a file, a
  // link to one or a link that points nowhere: the picture is encoded in
  // memory and put in place by replaceFile, which writes a new file beside
  // the name and renames it over. (Asking first whether the name is taken
  // would follow a link, and call a dangling one free.)
  const std::unique_ptr<SDL_IOStream, IoCloser> encoded(SDL_IOFromDynamicMem());
  if (!encoded || !SDL_SavePNG_IO(picture, encoded.get(), false)) {
    std::fprintf(stderr, "SDL_SavePNG_IO failed: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }
  const auto* const data = static_cast<const char*>(SDL_GetPointerProperty(
      SDL_GetIOProperties(encoded.get()), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
  const Sint64 size = SDL_GetIOSize(encoded.get());
  if (data == nullptr || size <= 0) {
    std::fputs("encoding the PNG produced nothing\n", stderr);
    return EXIT_FAILURE;
  }
  // A picture is for opening and attaching: ordinary permissions, not the
  // score file's owner-only ones.
  if (!pinball::replaceFile(request.output, std::string_view(data, static_cast<std::size_t>(size)),
                            pinball::FileAccess::Shared)) {
    std::fprintf(stderr, "cannot write %s\n", request.output.c_str());
    return EXIT_FAILURE;
  }
  std::printf("wrote %s after %u steps (%d x %d)\n", request.output.c_str(),
              static_cast<unsigned>(request.steps), picture->w, picture->h);
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char* argv[]) {
  const auto request =
      parseArguments(std::span<char* const>(argv, static_cast<std::size_t>(std::max(argc, 0))));
  if (!request) {
    std::fputs(kUsage, stderr);
    return EXIT_FAILURE;
  }

  int exitCode = EXIT_FAILURE;
  try {
    exitCode = run(*request);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Fatal: %s\n", e.what());
  } catch (...) {
    std::fputs("Fatal: unknown error\n", stderr);
  }
  SDL_Quit();
  return exitCode;
}
