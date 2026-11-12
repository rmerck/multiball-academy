// Development tool: runs the game headless with scripted input and writes one
// rendered frame to a PNG in the current directory. No window is opened.
// Used to review art and layout, and by ctest as a rendering smoke test.
//
// Usage: pinball_snapshot <steps> <name.png>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <string_view>

#include "core/Args.hpp"
#include "core/Game.hpp"
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

// True if the playfield part of the frame holds a real picture: many
// different colours, not a flat fill or a few lines. The panel area is ignored, because it
// is filled even when nothing else draws.
bool hasContent(SDL_Surface* surface) {
  constexpr int kPlayfieldWidth = 768;
  constexpr std::size_t kNeeded = 16;
  if (!SDL_LockSurface(surface)) {
    return false;
  }
  std::array<std::uint32_t, kNeeded> seen{};
  std::size_t distinct = 0;
  const auto* pixels = static_cast<const std::uint8_t*>(surface->pixels);
  const int width = surface->w < kPlayfieldWidth ? surface->w : kPlayfieldWidth;
  // Sample every fourth pixel of every fourth row: plenty, and quick.
  for (int y = 0; y < surface->h && distinct < kNeeded; y += 4) {
    const std::uint8_t* row =
        pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->pitch);
    for (int x = 0; x < width && distinct < kNeeded; x += 4) {
      std::uint32_t value = 0;
      std::memcpy(&value, row + static_cast<std::size_t>(x) * 4U, sizeof value);
      bool known = false;
      for (std::size_t i = 0; i < distinct; ++i) {
        known = known || seen[i] == value;
      }
      if (!known) {
        seen[distinct++] = value;
      }
    }
  }
  SDL_UnlockSurface(surface);
  return distinct >= kNeeded;
}

int run(std::uint32_t steps, const std::string& outputName) {
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
    SDL_RenderPresent(renderer.get());
  }

  if (!hasContent(surface.get())) {
    std::fputs("rendered frame is blank\n", stderr);
    return EXIT_FAILURE;
  }
  // Never write through whatever already has this name: remove it first, so
  // a symbolic link is replaced rather than followed.
  SDL_PathInfo existing{};
  if (SDL_GetPathInfo(outputName.c_str(), &existing)) {
    if (existing.type == SDL_PATHTYPE_DIRECTORY || !SDL_RemovePath(outputName.c_str())) {
      std::fprintf(stderr, "cannot replace existing %s\n", outputName.c_str());
      return EXIT_FAILURE;
    }
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
  if (argc != 3) {
    std::fputs("Usage: pinball_snapshot <steps 1..1000000> <name.png>\n", stderr);
    return EXIT_FAILURE;
  }
  const auto steps = pinball::parseFrameLimit(argv[1]);
  const std::string_view name = argv[2];
  if (!steps || !isPlainPngName(name)) {
    std::fputs("Usage: pinball_snapshot <steps 1..1000000> <name.png>\n", stderr);
    return EXIT_FAILURE;
  }

  int exitCode = EXIT_FAILURE;
  try {
    exitCode = run(*steps, std::string(name));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Fatal: %s\n", e.what());
  }
  SDL_Quit();
  return exitCode;
}
