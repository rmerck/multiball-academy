// Development tool: runs the game headless with scripted input and writes one
// rendered frame to a PNG in the current directory. No window is opened.
// Used to review art and layout, and by ctest as a rendering smoke test.
//
// Usage: pinball_snapshot <steps> <name.png> [about]
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <string_view>

#include "core/Args.hpp"
#include "core/Game.hpp"
#include "core/HighScores.hpp"
#include "platform/About.hpp"
#include "platform/Assets.hpp"
#include "platform/Canvas.hpp"
#include "platform/TableRenderer.hpp"

namespace {

struct SurfaceDeleter {
  void operator()(SDL_Surface* s) const noexcept { SDL_DestroySurface(s); }
};
struct RendererDeleter {
  void operator()(SDL_Renderer* r) const noexcept { SDL_DestroyRenderer(r); }
};

// The output is always a plain file name in the current directory: lower-case
// letters, digits and underscores, ending in ".png". No path separators.
bool isPlainPngName(std::string_view name) noexcept {
  constexpr std::string_view kExtension = ".png";
  if (name.size() <= kExtension.size() || name.size() > 64 ||
      name.substr(name.size() - kExtension.size()) != kExtension) {
    return false;
  }
  for (const char c : name.substr(0, name.size() - kExtension.size())) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
      return false;
    }
  }
  return true;
}

// A fixed script that exercises the table: launch, then work the flippers.
pinball::InputState scriptedInput(std::uint32_t step) noexcept {
  pinball::InputState input;
  input.plunger = (step % 1200U) >= 20U && (step % 1200U) < 140U;
  input.leftFlipper = (step / 29U) % 2U == 0U;
  input.rightFlipper = (step / 41U) % 2U == 0U;
  return input;
}

// True if the frame has more than one colour, i.e. something was drawn.
bool hasContent(SDL_Surface* surface) {
  if (!SDL_LockSurface(surface)) {
    return false;
  }
  const auto* pixels = static_cast<const std::uint8_t*>(surface->pixels);
  const std::uint32_t* first = reinterpret_cast<const std::uint32_t*>(pixels);  // NOLINT
  bool varied = false;
  for (int y = 0; y < surface->h && !varied; ++y) {
    const auto* row =
        reinterpret_cast<const std::uint32_t*>(pixels + static_cast<std::size_t>(y) *  // NOLINT
                                                            static_cast<std::size_t>(surface->pitch));
    for (int x = 0; x < surface->w; ++x) {
      if (row[x] != *first) {
        varied = true;
        break;
      }
    }
  }
  SDL_UnlockSurface(surface);
  return varied;
}

int run(std::uint32_t steps, const std::string& outputName, bool showAbout) {
  const pinball::AssetLoader assets = pinball::AssetLoader::fromExecutableLocation();
  if (!assets.verifyAll()) {
    std::fputs("asset integrity check failed\n", stderr);
    return EXIT_FAILURE;
  }

  const std::unique_ptr<SDL_Surface, SurfaceDeleter> surface(
      SDL_CreateSurface(pinball::kCanvasWidth, pinball::kCanvasHeight, SDL_PIXELFORMAT_RGBA32));
  if (!surface) {
    std::fprintf(stderr, "SDL_CreateSurface failed: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }
  // Declared after the surface so it is destroyed first.
  const std::unique_ptr<SDL_Renderer, RendererDeleter> renderer(
      SDL_CreateSoftwareRenderer(surface.get()));
  if (!renderer) {
    std::fprintf(stderr, "SDL_CreateSoftwareRenderer failed: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  {
    const std::unique_ptr<pinball::TableRenderer> table =
        pinball::TableRenderer::create(renderer.get(), assets);
    if (!table) {
      return EXIT_FAILURE;
    }

    pinball::Game game;
    for (std::uint32_t step = 0; step < steps; ++step) {
      game.step(scriptedInput(step));
    }

    SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer.get());
    table->draw(renderer.get(), game, false);
    if (showAbout) {
      // With a sample table, so the layout of a full list can be checked.
      pinball::HighScores sample;
      sample.submit({12'345'678, 4});
      sample.submit({3'250'000, 2});
      sample.submit({987'500, 1});
      pinball::drawAbout(renderer.get(), sample);
    }
    SDL_RenderPresent(renderer.get());
  }

  if (!hasContent(surface.get())) {
    std::fputs("rendered frame is blank\n", stderr);
    return EXIT_FAILURE;
  }
  if (!SDL_SavePNG(surface.get(), outputName.c_str())) {
    std::fprintf(stderr, "SDL_SavePNG failed: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }
  std::printf("wrote %s after %u steps\n", outputName.c_str(), static_cast<unsigned>(steps));
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char* argv[]) {
  const bool showAbout = argc == 4 && std::string_view(argv[3]) == "about";
  if (argc != 3 && !showAbout) {
    std::fputs("Usage: pinball_snapshot <steps 1..1000000> <name.png> [about]\n", stderr);
    return EXIT_FAILURE;
  }
  const auto steps = pinball::parseFrameLimit(argv[1]);
  const std::string_view name = argv[2];
  if (!steps || !isPlainPngName(name)) {
    std::fputs("Usage: pinball_snapshot <steps 1..1000000> <name.png> [about]\n", stderr);
    return EXIT_FAILURE;
  }

  int exitCode = EXIT_FAILURE;
  try {
    exitCode = run(*steps, std::string(name), showAbout);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Fatal: %s\n", e.what());
  }
  SDL_Quit();
  return exitCode;
}
