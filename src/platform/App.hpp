#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/AboutPages.hpp"
#include "core/FixedTimestep.hpp"
#include "core/Game.hpp"
#include "core/Input.hpp"
#include "core/HighScores.hpp"
#include "core/InitialsEntry.hpp"
#include "core/LoopPolicy.hpp"
#include "core/ScreenLayout.hpp"
#include "core/Sound.hpp"
#include "platform/AudioOutput.hpp"
#include "platform/ScoreStore.hpp"
#include "platform/TableRenderer.hpp"

namespace pinball {

struct AppOptions {
  // Exit after this many rendered frames. Used by automated smoke tests.
  std::optional<std::uint32_t> frameLimit;
  // Keyboard and a fixed canvas, or touch controls and a canvas that
  // follows the window.
  screen::Idiom idiom = screen::Idiom::Desktop;
};

enum class FrameResult : std::uint8_t { Continue, Quit };

// Owns the SDL video subsystem, the window and the renderer, and runs the
// main loop: run() on a desktop, or frame() once per display refresh where
// the system owns the loop.
class App {
 public:
  // Returns nullptr and logs the SDL error if any resource fails to initialise.
  [[nodiscard]] static std::unique_ptr<App> create(const AppOptions& options);

  ~App();
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  // Takes one event. Events from SDL's queue are read by frame() itself;
  // this is for those the system delivers outside the queue.
  void handleEvent(const SDL_Event& event);
  // One turn of the loop: pending events, input, the simulation steps that
  // are due, sound, and a drawn frame. Never sleeps. Quit once the player
  // has quit or the frame limit is reached.
  [[nodiscard]] FrameResult frame();
  // To be called once, after the last frame: keeps a score that is waiting
  // at the initials prompt and returns the exit code.
  [[nodiscard]] int finish();
  // The desktop's loop: frame() until it says Quit, sleeping while nothing
  // moves and pacing the frames when the display does not, then finish().
  [[nodiscard]] int run();

 private:
  struct WindowDeleter {
    void operator()(SDL_Window* w) const noexcept { SDL_DestroyWindow(w); }
  };
  struct RendererDeleter {
    void operator()(SDL_Renderer* r) const noexcept { SDL_DestroyRenderer(r); }
  };

  App(const AppOptions& options, const screen::Layout& layout, SDL_Window* window,
      SDL_Renderer* renderer, bool vsync, std::unique_ptr<TableRenderer> table);

  // In AppKeyboard.cpp.
  void handleKeyDown(const SDL_KeyboardEvent& key);
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
  // In AppTouch.cpp: a finger went down, moved or lifted.
  void handleTouch(const SDL_Event& event);
  // In AppLifecycle.cpp: the window gained or lost focus, or was minimised
  // or restored.
  void handleFocusChange();
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
  // Where everything is drawn. Fixed for the desktop idiom.
  screen::Layout layout_;
  // Members are destroyed in reverse order: textures, renderer, then window.
  std::unique_ptr<SDL_Window, WindowDeleter> window_;
  std::unique_ptr<SDL_Renderer, RendererDeleter> renderer_;
  // Its textures belong to the renderer; declared after it so it is
  // destroyed first.
  std::unique_ptr<TableRenderer> table_;
  FixedTimestep timestep_{Game::kStepSeconds, 8};
  // Keeps time spent standing still out of the simulation (see frame()).
  PauseClock clock_;
  // SDL_GetTicksNS() when the previous frame and this one began.
  std::uint64_t previousNs_ = 0;
  std::uint64_t frameStartNs_ = 0;
  // Frames drawn, and how many of the latest in a row were halted.
  std::uint32_t frames_ = 0;
  std::uint32_t haltedFrames_ = 0;
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
  // Which page of it is up. Back to the first whenever it opens.
  AboutState aboutPage_{};
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
