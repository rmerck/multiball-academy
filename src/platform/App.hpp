#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/FixedTimestep.hpp"
#include "core/Game.hpp"
#include "core/Input.hpp"
#include "platform/Assets.hpp"

namespace pinball {

struct AppOptions {
  // Exit after this many rendered frames. Used by automated smoke tests.
  std::optional<std::uint32_t> frameLimit;
};

// Owns the SDL video subsystem, the window and the renderer, and runs the main loop.
class App {
 public:
  // Logical canvas. 4:3 like the original, at twice 640x480.
  static constexpr int kLogicalWidth = 1280;
  static constexpr int kLogicalHeight = 960;

  // Returns nullptr and logs the SDL error if any resource fails to initialise.
  static std::unique_ptr<App> create(const AppOptions& options);

  ~App();
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  // Runs until the user quits or the frame limit is reached. Returns exit code.
  int run();

 private:
  struct WindowDeleter {
    void operator()(SDL_Window* w) const noexcept { SDL_DestroyWindow(w); }
  };
  struct RendererDeleter {
    void operator()(SDL_Renderer* r) const noexcept { SDL_DestroyRenderer(r); }
  };

  // Every texture the game draws. Loaded once at startup.
  struct Textures {
    TexturePtr ball;
  };

  App(const AppOptions& options, SDL_Window* window, SDL_Renderer* renderer, bool vsync,
      Textures textures);

  void handleEvent(const SDL_Event& event);
  void readInput();
  void render(double alpha);
  void drawTableOutline();
  void toggleFullscreen();
  // True when presenting will not block, so the loop must sleep on its own.
  bool needsManualPacing() const;

  AppOptions options_;
  // Members are destroyed in reverse order: textures, renderer, then window.
  std::unique_ptr<SDL_Window, WindowDeleter> window_;
  std::unique_ptr<SDL_Renderer, RendererDeleter> renderer_;
  // Textures belong to the renderer and are declared after it so they are
  // destroyed first.
  Textures textures_;
  FixedTimestep timestep_{Game::kStepSeconds, 8};
  bool vsync_ = false;
  bool running_ = true;
  bool paused_ = false;
  std::vector<SDL_FPoint> outline_;
  Game game_;
  InputState input_;
};

}  // namespace pinball
