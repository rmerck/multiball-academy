#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/FixedTimestep.hpp"
#include "core/Game.hpp"
#include "core/Input.hpp"
#include "core/HighScores.hpp"
#include "core/InitialsEntry.hpp"
#include "core/LoopPolicy.hpp"
#include "core/ScreenLayout.hpp"
#include "core/Sound.hpp"
#include "core/TouchControls.hpp"
#include "platform/About.hpp"
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
  // Open the audio device on a thread of its own and start silent, trying
  // again later if no device opens. For systems where the open can take
  // seconds (see AudioOpener).
  bool backgroundAudioOpen = false;
  // Read the player's high score file at start and write it when a score
  // is entered. Off, the game starts with an empty table and leaves the
  // file alone, as a timed run always does. For tests that play as a
  // player would.
  bool keepScores = true;
  // Give the simulation this many seconds for every frame, whatever the
  // clock says, so that a test's game runs the same on any machine and as
  // fast as the machine can go.
  std::optional<double> fixedFrameSeconds;
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
  //
  // For a loop the system drives, once per display refresh. Such a loop
  // cannot sleep while the game stands still, so a frame that would show
  // the same picture as the last one is not drawn again, and nothing is
  // drawn while the application is in the background. (A timed automated
  // run draws every frame.)
  [[nodiscard]] FrameResult frame();
  // After the last frame: keeps a score that is waiting at the initials
  // prompt and returns the exit code. Calling it again is harmless.
  [[nodiscard]] int finish();
  // The desktop's loop: frame() until it says Quit, sleeping while nothing
  // moves and pacing the frames when the display does not, then finish().
  [[nodiscard]] int run();

  // What the application is showing and doing, for tests and diagnostics.
  struct Status {
    bool paused;
    bool autoPaused;
    // The pause came from an interruption on a touch screen, not from the
    // player.
    bool interrupted;
    bool aboutOpen;
    bool entryOpen;
    bool backgrounded;
    bool muted;
    bool musicOn;
    bool confirmNewGame;
    bool audioOpen;
    bool gameOver;
    AboutState about;
    // The letters and cursor of the initials prompt; AAA and 0 without one.
    Initials initials;
    std::size_t cursor;
    std::uint32_t frames;
    // Frames that were drawn and presented; the rest showed nothing new.
    std::uint32_t drawnFrames;
    std::uint64_t steps;
    // What the latest simulation step was given.
    InputState input;
    screen::Layout layout;
    TouchControls::View touch;
  };
  [[nodiscard]] Status status() const;

 private:
  struct WindowDeleter {
    void operator()(SDL_Window* w) const noexcept { SDL_DestroyWindow(w); }
  };
  struct RendererDeleter {
    void operator()(SDL_Renderer* r) const noexcept { SDL_DestroyRenderer(r); }
  };

  App(const AppOptions& options, const screen::Layout& layout, SDL_Window* window,
      SDL_Renderer* renderer, bool vsync, std::unique_ptr<TableRenderer> table);

  // How long the initials prompt ignores keys and touches after it appears.
  static constexpr Uint64 kEntryGuardMs = 700;
  // What is in front of the player. Fingers on one do not carry to the next.
  enum class Screen : std::uint8_t { Play, Menu, About, Prompt };

  // One turn of the loop. restWhenStill: do not redraw a picture that has
  // not changed.
  [[nodiscard]] FrameResult iterate(bool restWhenStill);
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
  void touchMenu(OverlayTarget acted);
  void touchAbout(OverlayTarget acted);
  void touchInitials(OverlayTarget acted);
  // A held letter arrow of the initials prompt repeats.
  void repeatHeldArrow();
  [[nodiscard]] Screen screen() const noexcept;
  // Lets go of every finger when what is in front of the player changes.
  void syncScreen();
  // What the centre button depends on.
  [[nodiscard]] TouchControls::Context touchContext() const noexcept;
  // Leaves the pause menu for the game.
  void resumePlay();
  // The window changed size or safe area: the touch layout follows it.
  void handleLayoutChange();
  // In AppLifecycle.cpp: the window gained or lost focus, or was minimised
  // or restored.
  void handleFocusChange();
  // The application is going to the background, has come back, is being
  // ended, or was told that memory is short.
  void handleLifecycle(Uint32 type);
  // Stops the game for something the player did not choose: every control
  // is let go, and on a touch screen the game stays paused until the player
  // resumes it. Timed automated runs have no player and are never paused.
  void interrupt();
  // Takes a device the background open has produced, and starts another
  // open when one is due.
  void serviceAudio();
  // Lets the screen sleep while nothing is being played (touch screens).
  void updateIdleTimer();
  // cursorLit: the blinking cursor of the initials prompt shows.
  void render(double alpha, bool cursorLit);
  // Notes a failed renderer call, logging the first one only.
  void noteRenderFailure(const char* call);
  // Loads every texture again after the graphics device was reset. Keeps the
  // old ones, and logs, if that fails.
  void reloadTextures();
  void toggleFullscreen();
  // True when presenting will not block, so the loop must sleep on its own.
  bool needsManualPacing() const;

  AppOptions options_;
  // Where everything is drawn. Fixed for the desktop idiom; follows the
  // window for the touch idiom.
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
  // Turns of the loop, how many of them drew a frame, and how many of the
  // latest in a row were halted.
  std::uint32_t frames_ = 0;
  std::uint32_t drawnFrames_ = 0;
  std::uint32_t haltedFrames_ = 0;
  // Something may have changed what the picture shows since it was drawn.
  bool needsRedraw_ = true;
  // The cursor of the initials prompt as last drawn.
  bool cursorWasLit_ = false;
  // Between "will enter background" and "did enter foreground": nothing is
  // drawn.
  bool backgrounded_ = false;
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
  AboutState aboutState_;
  // Touch idiom: the pause is from an interruption, and NEW GAME in the
  // pause menu was tapped once and waits for the second tap.
  bool interrupted_ = false;
  bool confirmNewGame_ = false;
  // The system's Reduce Motion setting, read at start and on every return
  // to the foreground.
  bool reduceMotion_ = false;
  // Whether the screen is being kept awake; unset until first decided.
  std::optional<bool> keepingAwake_;
  // Only with AppOptions::backgroundAudioOpen. Declared before audio_, so
  // that the device is closed before the opener.
  std::optional<AudioOpener> audioOpener_;
  // Another open is wanted (the application came back to the foreground, or
  // the audio route changed), and when the last one was started.
  bool audioRetryWanted_ = false;
  bool audioAttempted_ = false;
  Uint64 audioAttemptMs_ = 0;
  unsigned audioRouteChangesSeen_ = 0;
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
  // Keys and touches that went down since the last simulation step, held
  // until a step has consumed them.
  InputState latched_;
  // Fingers on the playing controls, and the finger on an overlay.
  TouchControls touch_;
  OverlayTaps overlayTaps_;
  Screen lastScreen_ = Screen::Play;
};

}  // namespace pinball
