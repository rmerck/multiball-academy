#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/AboutPages.hpp"
#include "core/FixedTimestep.hpp"
#include "core/Game.hpp"
#include "core/Input.hpp"
#include "core/HighScores.hpp"
#include "core/InitialsEntry.hpp"
#include "core/LoopPolicy.hpp"
#include "core/ScreenLayout.hpp"
#include "core/Snapshot.hpp"
#include "core/Sound.hpp"
#include "core/Args.hpp"
#include "core/Attract.hpp"
#include "core/Feedback.hpp"
#include "core/FrontMenu.hpp"
#include "core/Settings.hpp"
#include "core/TouchControls.hpp"
#include "platform/About.hpp"
#include "platform/AudioOutput.hpp"
#include "platform/SaveStore.hpp"
#include "platform/ScoreStore.hpp"
#include "core/LightShow.hpp"
#include "platform/SettingsStore.hpp"
#include "platform/TableRenderer.hpp"
#include "platform/ios/IosSystem.hpp"

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
  // is entered; and keep a game in progress when the application leaves the
  // foreground or quits, to be resumed (docs/RESUME.md). Off, the game
  // starts with an empty table and leaves both files alone, as a timed run
  // and a staged scene always do. For tests that play as a player would.
  bool keepScores = true;
  // Give the simulation this many seconds for every frame, whatever the
  // clock says, so that a test's game runs the same on any machine and as
  // fast as the machine can go.
  std::optional<double> fixedFrameSeconds;
  // Start in a staged moment of a game (core/Scenes.hpp), for screenshots.
  // A staged game's score is not the player's: the score file is left alone
  // as for a timed run. Only debug builds read it from the command line.
  std::optional<Scene> scene;
  // Whether a touch that starts in a bottom corner of the window is left to
  // the system, which resizes the window from there (inSystemCorner). By
  // default where the system does that: on a tablet.
  std::optional<bool> systemCorners;
  // Open on the title screen, with attract mode behind it, as a player
  // does. Never for a timed run or a staged scene, which go straight to
  // the game. Off for tests that start with the game itself.
  bool showTitle = true;
  // For tests: attract mode's game takes this many steps every frame,
  // whatever the clock, so that a test can see attract games end. 1 to
  // kMaxAttractStepsPerFrame.
  std::optional<int> attractStepsPerFrame;
  // Less motion, as the system's Reduce Motion setting asks: given, it is
  // used instead of the system's, which is read otherwise (at start and on
  // every return to the foreground). Tests give it, so that they do not
  // depend on the machine's accessibility settings.
  std::optional<bool> reduceMotion;
  // Where the high scores and the saved game are kept (a separator is
  // added at the end if missing); empty for the player's preferences
  // directory. For tests,
  // which give a scratch directory so that the player's files are never
  // touched; no command line sets it.
  std::string dataDirectory;
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

  // --- The saved game (docs/RESUME.md) ----------------------------------------
  // For the title menu: a game left in progress when the application last
  // went to the background or quit, checked and ready to resume. Only that
  // menu's CONTINUE should depend on it.
  [[nodiscard]] bool hasSavedGame() const noexcept { return savedGame_.has_value(); }
  // Puts the saved game on the table in place of the one there, paused,
  // exactly as it was left. The file is deleted first: a save is resumed
  // once, and if it cannot be deleted it is not resumed (false). False too
  // with no saved game.
  bool resumeSavedGame();
  // Deletes the saved game, if there is one. Starting a new game and
  // finishing one do this by themselves; a menu may call it for a PLAY that
  // starts nothing yet.
  void discardSavedGame();

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
    // The device is open and stopped, as it must be while the application
    // is not in front.
    bool audioSuspended;
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
    // The front screens (title, scores, how to play, settings) are up, and
    // which; the title has finished building itself.
    bool frontOpen;
    FrontState front;
    bool introDone;
    // Simulation steps of the attract game since the title last opened,
    // and attract games that have ended in this run.
    std::uint64_t attractSteps;
    unsigned attractGames;
    // The touch guide before the first game is up.
    bool guideOpen;
    // MAIN MENU was chosen once in the pause and waits for a second press.
    bool confirmMainMenu;
    Settings settings;
    // Store reviews asked of the system in this run.
    unsigned reviewRequests;
    // Haptics played in this run.
    unsigned hapticsPlayed;
    // The high score table, as shown.
    HighScores scores;
    // The game drawn behind HOW TO PLAY's touch guide while it is up (a new
    // game's: no score, no step taken), and what its touch controls show.
    // 0, 0 and the player's controls when that guide is not up.
    std::uint64_t guideTableScore;
    std::uint64_t guideTableSteps;
    TouchControls::View guideTouch;
  };
  [[nodiscard]] Status status() const;
  // How many frames a standing picture is drawn for after each event.
  static constexpr std::uint32_t kRedrawsAfterEvent = 3;
  // A finished game goes back to the title after this long with nothing
  // touched, or on a touch or key after the guard (fingers still on the
  // flippers when the last ball drains are not a request).
  static constexpr double kGameOverIdleSeconds = 8.0;
  static constexpr double kGameOverGuardSeconds = 0.8;
  static constexpr int kMaxAttractStepsPerFrame = 2000;

  // What VoiceOver finds on the screen now: one element for each control,
  // title and block of text, in reading order. Frames are in canvas units.
  // Empty on a desktop and while the game is played. The same list is
  // handed to the system (ios::setAccessibilityElements); for tests too.
  struct AccessItem {
    std::string label;
    screen::Rect frame;
    std::int32_t code = 0;
    ios::AccessRole role = ios::AccessRole::Button;
    bool enabled = true;
  };
  [[nodiscard]] std::vector<AccessItem> accessibilityItems() const;

  // The system's Reduce Motion setting as the game last read it (at start
  // and on every return to the foreground): the one place everything that
  // moves for show asks, the title's animation and attract mode's lights
  // included.
  [[nodiscard]] bool reduceMotion() const noexcept { return reduceMotion_; }

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
  enum class Screen : std::uint8_t { Play, Menu, About, Prompt, Front, Guide };

  // One turn of the loop. restWhenStill: do not redraw a picture that has
  // not changed.
  [[nodiscard]] FrameResult iterate(bool restWhenStill);
  // In AppKeyboard.cpp.
  void handleKeyDown(const SDL_KeyboardEvent& key);
  void readInput();
  // True when the simulation should stand still.
  bool halted() const noexcept {
    return paused_ || autoPaused_ || aboutOpen_ || entry_.has_value() || frontOpen_ || guideOpen_;
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
  // In AppFront.cpp: the front screens. Opens the title, attract mode
  // behind it, from the start or from the pause menu.
  void openFront();
  // From the title: a new game, after the touch guide if it is the
  // player's first.
  void startGame();
  // Leaves the game for the title (MAIN MENU in the pause).
  void goToMainMenu();
  [[nodiscard]] FrontConfig frontConfig() const noexcept;
  [[nodiscard]] bool introDone() const noexcept;
  void skipIntro();
  void frontAct(FrontAction action);
  void handleFrontKey(const SDL_KeyboardEvent& key);
  void touchFront(TouchPhase phase, std::uint64_t id, float x, float y);
  // The touch guide before the first game is dismissed.
  void dismissGuide();
  // HOW TO PLAY's touch guide: a new game for it to show the controls over.
  void prepareGuideGame();
  // Steps attract mode's game for the time given.
  void stepAttract(double seconds);
  // Settings: applies them to the sound, and saves them if this run keeps
  // the player's files.
  void applySettings();
  void saveSettings();
  void setSound(bool on);
  void setMusic(bool on);
  // A haptic for what the frame's sounds say happened, if one is due.
  void playHaptics(SoundSet sounds);
  // At the title, after a game, now and then: the system's review prompt.
  void maybeRequestReview();
  // What VoiceOver finds on the screen, when that has changed.
  void refreshAccessibility();
  // An accessibility element was activated.
  void handleAccessibilityAction(std::int32_t code);
  // CONTINUE on the title: the saved game, paused, in place of the title.
  void continueSavedGame();
  // A finished game is on the table with nothing over it (no initials
  // prompt, pause or About) in a run that has the title: it goes back to
  // the title after kGameOverIdleSeconds, or at once on a tap or a key
  // once kGameOverGuardSeconds have passed.
  [[nodiscard]] bool gameOverStanding() const noexcept;
  // Attract mode's lamps, for the table drawn from its layout: those the
  // light show's attract sequence (attractShow_) has lit. With the art, the
  // show itself is drawn.
  [[nodiscard]] TableLights attractLights(const Game& attract) const;
  // The window changed size or safe area: the touch layout follows it.
  void handleLayoutChange();
  // In AppLifecycle.cpp: the window gained or lost focus, or was minimised
  // or restored.
  void handleFocusChange(Uint32 type);
  // The application is going to the background, has come back, is being
  // ended, or was told that memory is short.
  void handleLifecycle(Uint32 type);
  // Stops the game for something the player did not choose: every control
  // is let go, and on a touch screen the game stays paused until the player
  // resumes it. Timed automated runs have no player and are never paused.
  void interrupt();
  // The application is leaving the front: interrupt(), and no sound.
  void leaveForeground();
  // Whether this run keeps the player's saved game (AppOptions::keepScores,
  // and never for a timed run or a staged scene).
  [[nodiscard]] bool keepsSavedGame() const noexcept;
  // Writes the game in progress to the saved-game file (when leaving the
  // foreground, being ended or quitting). Nothing for a game not in
  // progress, or one already written as it stands.
  void saveGameInProgress();
  // Back in front with the game still in memory: the file written on the
  // way out goes, so that no copy of a game is left behind it.
  void withdrawSavedGame();
  // After each step: a game begun or ended on the table makes the saved one
  // stale, and it goes.
  void followSavedGame();
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
  // Frames still to be drawn although the game stands still: something
  // may have changed what the picture shows. More than one for each event,
  // because the drawable can change a frame or two after the event that
  // announced it (a turned tablet, a resized window, a return to the
  // foreground) with nothing further to say so. A frame that could not be
  // presented does not count.
  std::uint32_t redrawsDue_ = kRedrawsAfterEvent;
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
  // Which page of it is up. Back to the first whenever it opens.
  AboutState aboutPage_{};
  // Touch idiom: the pause is from an interruption, and NEW GAME in the
  // pause menu was tapped once and waits for the second tap.
  bool interrupted_ = false;
  bool confirmNewGame_ = false;
  // The system's Reduce Motion setting, read at start and on every return
  // to the foreground.
  bool reduceMotion_ = false;
  // Touches from the window's bottom corners are the system's (AppOptions).
  bool systemCorners_ = false;
  // Whether the screen is being kept awake; unset until first decided.
  std::optional<bool> keepingAwake_;
  // Only with AppOptions::backgroundAudioOpen. Declared before audio_, so
  // that the device is closed before the opener.
  //
  // Its destructor waits for an open still in flight, on the thread that
  // destroys the App. Where the open is stuck (a simulator with no audio
  // route: about 30 seconds) quitting waits that long. Nothing is lost by
  // it: finish(), which keeps a score waiting at the initials prompt, is
  // called before the App is destroyed (main.cpp, main_ios.cpp), and the
  // score file is written when initials are confirmed, never at exit.
  std::optional<AudioOpener> audioOpener_;
  // Another open is wanted (the application came back to the foreground, or
  // a device came or went); one is in flight; and when the last one ended,
  // from which the wait before the next is counted.
  bool audioRetryWanted_ = false;
  bool audioAttempted_ = false;
  bool audioOpening_ = false;
  Uint64 audioAttemptMs_ = 0;
  // Between going to the background and coming back: the device stays
  // stopped, also one that finishes opening meanwhile.
  bool audioSuspended_ = false;
  unsigned audioRouteChangesSeen_ = 0;
  // Null when no audio device could be opened: the game then runs silent.
  std::unique_ptr<AudioOutput> audio_;
  SoundMixer mixer_;
  // The player's choice. The tune also rests whenever the game is halted.
  bool musicOn_ = true;
  Game game_;
  // The table's lights, fed every step of game_ (core/LightShow.hpp), and
  // the attract game's, fed every step of attract_ with the attract
  // sequence on (off on the HOW TO PLAY guide, which shows the table as
  // played). Reduce motion reaches both from reduceMotion_.
  LightShow lightShow_;
  LightShow attractShow_;
  // Declared before the table it loads.
  ScoreStore scoreStore_;
  // The saved game: the one offered to resume, read at start; whether the
  // file may be on disk; whether this run wrote it on the way out, and at
  // which step; and the table's count of games begun when last looked at.
  SaveStore saveStore_;
  std::optional<save::Snapshot> savedGame_;
  bool saveOnDisk_ = false;
  bool wroteSave_ = false;
  std::uint64_t savedAtStep_ = 0;
  std::uint32_t gamesBegunSeen_ = 0;
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

  // The front screens.
  FrontMenu front_;
  bool frontOpen_ = false;
  // Seconds since the title opened (its intro), and a clock for what moves
  // on it after that.
  double introSeconds_ = 0.0;
  double frontSeconds_ = 0.0;
  // The finger on a front screen, what it went down on, and whether it is
  // still there.
  std::optional<std::uint64_t> frontFinger_;
  FrontTarget frontDown_{};
  bool frontOnTarget_ = false;
  // Attract mode: a game of its own, never the player's, and its hands.
  std::optional<Game> attract_;
  // HOW TO PLAY's touch guide shows the controls over a new game of its
  // own, never attract mode's or a game left by MAIN MENU, with its lights.
  std::optional<Game> guideGame_;
  LightShow guideShow_;
  AttractPilot pilot_;
  FixedTimestep attractTimestep_{Game::kStepSeconds, 8};
  std::uint64_t attractSteps_ = 0;
  unsigned attractGames_ = 0;
  // The touch guide before the player's first game.
  bool guideOpen_ = false;
  bool confirmMainMenu_ = false;
  // This run has the title (AppOptions::showTitle, and not a timed run or
  // a staged scene); and how long a finished game has stood with nothing
  // over it, after which the title comes back.
  bool titleRun_ = false;
  double gameOverIdle_ = 0.0;
  // The player's settings, and whether this run reads and writes them.
  Settings settings_;
  SettingsStore settingsStore_;
  bool keepFiles_ = false;
  // Whether the device has haptics.
  bool hapticsAvailable_ = false;
  HapticGate hapticGate_;
  unsigned hapticsPlayed_ = 0;
  // A player's game ended in this run: the review may be asked for.
  bool gameEndedThisRun_ = false;
  unsigned reviewRequests_ = 0;
  // Event type of an activated accessibility element, and what the
  // elements last given to the system were made from.
  Uint32 accessibilityEvent_ = 0;
  std::uint64_t accessibilityKey_ = ~std::uint64_t{0};
  std::uint64_t accessibilityPage_ = ~std::uint64_t{0};
};

}  // namespace pinball
