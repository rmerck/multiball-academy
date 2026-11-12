#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/FixedTimestep.hpp"
#include "core/Game.hpp"
#include "core/Input.hpp"
#include "platform/TableRenderer.hpp"

namespace pinball {

struct AppOptions {
  // Exit after this many rendered frames. Used by automated smoke tests.
  std::optional<std::uint32_t> frameLimit;
};

// Owns the SDL video subsystem, the window and the renderer, and runs the main loop.
class App {
 public:
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

  App(const AppOptions& options, SDL_Window* window, SDL_Renderer* renderer, bool vsync,
      std::unique_ptr<TableRenderer> table);

  void handleEvent(const SDL_Event& event);
  void readInput();
  void latchKey(SDL_Scancode scancode);
  void render(double alpha);
  void toggleFullscreen();
  // True when presenting will not block, so the loop must sleep on its own.
  bool needsManualPacing() const;

  AppOptions options_;
  // Members are destroyed in reverse order: textures, renderer, then window.
  std::unique_ptr<SDL_Window, WindowDeleter> window_;
  std::unique_ptr<SDL_Renderer, RendererDeleter> renderer_;
  // Its textures belong to the renderer; declared after it so it is
  // destroyed first.
  std::unique_ptr<TableRenderer> table_;
  FixedTimestep timestep_{Game::kStepSeconds, 8};
  bool vsync_ = false;
  bool running_ = true;
  bool paused_ = false;
  Game game_;
  InputState input_;
  // Keys that went down since the last simulation step, held until a step
  // has consumed them.
  InputState latched_;
};

}  // namespace pinball
