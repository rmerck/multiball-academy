#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/FixedTimestep.hpp"
#include "core/Game.hpp"
#include "core/Input.hpp"
#include "core/HighScores.hpp"
#include "core/InitialsEntry.hpp"
#include "core/Sound.hpp"
#include "platform/AudioOutput.hpp"
#include "platform/ScoreStore.hpp"
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
  // True when the simulation should stand still.
  bool halted() const noexcept {
    return paused_ || autoPaused_ || aboutOpen_ || entry_.has_value();
  }
  // Key presses while the initials prompt is up.
  void handleEntryKey(SDL_Keycode key, SDL_Keymod modifiers);
  // Enters the finished game in the table under the entered initials. Does
  // nothing if the prompt is not up.
  void commitInitials();
  void recordFinishedGame();
  void latchKey(SDL_Scancode scancode);
  void render(double alpha);
  // Notes a failed renderer call, logging the first one only.
  void noteRenderFailure(const char* call);
  // Loads every texture again after the graphics device was reset. Keeps the
  // old ones, and logs, if that fails.
  void reloadTextures();
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
  // The last frame was presented. When it was not, presenting did not wait
  // for the display either.
  bool presented_ = true;
  bool renderFailureLogged_ = false;
  // The graphics device is gone for good; the loop ends with an error.
  bool deviceLost_ = false;
  bool running_ = true;
  bool paused_ = false;
  // Paused because the window lost focus or was minimised. Kept apart from
  // the player's own pause so that regaining focus does not undo F3.
  bool autoPaused_ = false;
  // The About screen is up. The game stands still behind it.
  bool aboutOpen_ = false;
  // Null when no audio device could be opened: the game then runs silent.
  std::unique_ptr<AudioOutput> audio_;
  SoundMixer mixer_;
  // The player's choice. The tune also rests whenever the game is halted.
  bool musicOn_ = true;
  Game game_;
  // Declared before the table it loads.
  ScoreStore scoreStore_;
  // Best games on this machine, loaded at start and saved when a game ends.
  // A timed automated run starts with an empty table and saves nothing.
  HighScores highScores_;
  // Event type the system menu's About item sends, or 0 if none could be
  // registered.
  Uint32 aboutEvent_ = 0;
  // Present while the player is entering initials for pending_. The game
  // stands still behind the prompt.
  std::optional<InitialsEntry> entry_;
  HighScore pending_;
  // SDL_GetTicks() when the prompt opened.
  Uint64 entryOpenedMs_ = 0;
  // Offered again next time, so a returning player only has to confirm.
  Initials lastInitials_{'A', 'A', 'A'};
  bool wasGameOver_ = false;
  bool newHighScore_ = false;
  InputState input_;
  // Keys that went down since the last simulation step, held until a step
  // has consumed them.
  InputState latched_;
};

}  // namespace pinball
