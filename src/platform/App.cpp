#include "platform/App.hpp"

#include <utility>

#include "core/Version.hpp"
#include "platform/About.hpp"
#include "platform/Canvas.hpp"
#include "platform/InitialsScreen.hpp"
#include "platform/MacMenu.hpp"
#include "platform/ScoreStore.hpp"
#include "platform/Text.hpp"

namespace pinball {

namespace {

// Frame period used when presenting does not block (no vsync, hidden window).
constexpr std::uint64_t kUnsyncedFrameNs = 1'000'000'000ULL / 120ULL;
// Hard floor on frame time that applies even when vsync is reported. 1000 FPS
// is above any display refresh rate, so it never costs a vblank, and it still
// stops a driver that ignores vsync from spinning a core flat out.
constexpr std::uint64_t kMinFrameNs = 1'000'000'000ULL / 1000ULL;
// Half period of the blinking cursor in the initials prompt.
constexpr Uint64 kCursorBlinkMs = 400;
// How long the initials prompt ignores keys after it appears.
constexpr Uint64 kEntryGuardMs = 700;

// Pairs SDL_InitSubSystem with SDL_QuitSubSystem on every exit path of create().
// The subsystem calls are reference counted, unlike SDL_Init/SDL_Quit.
class VideoSession {
 public:
  VideoSession() : ok_(SDL_InitSubSystem(SDL_INIT_VIDEO)) {}
  ~VideoSession() {
    if (ok_ && !released_) {
      SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
  }
  VideoSession(const VideoSession&) = delete;
  VideoSession& operator=(const VideoSession&) = delete;
  bool ok() const noexcept { return ok_; }
  // Hands responsibility for SDL_QuitSubSystem to the App.
  void release() noexcept { released_ = true; }

 private:
  bool ok_;
  bool released_ = false;
};

}  // namespace

std::unique_ptr<App> App::create(const AppOptions& options) {
  SDL_SetAppMetadata(kGameTitle, kGameVersion, "io.github.rmerck.spacecadetpinball");
  SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_CREATOR_STRING, "rmerck");
  SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_URL_STRING, "https://github.com/rmerck");

  // Refuse to start unless every asset matches the build. Done first, before
  // any window exists.
  const AssetLoader assets = AssetLoader::fromExecutableLocation();
  if (!assets.verifyAll()) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Asset integrity check failed; reinstall or rebuild the game");
    return nullptr;
  }

  VideoSession session;
  if (!session.ok()) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL video init failed: %s", SDL_GetError());
    return nullptr;
  }

  std::unique_ptr<SDL_Window, WindowDeleter> window(
      SDL_CreateWindow(kGameTitle, kCanvasWidth, kCanvasHeight,
                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY));
  if (!window) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed: %s", SDL_GetError());
    return nullptr;
  }

  std::unique_ptr<SDL_Renderer, RendererDeleter> renderer(
      SDL_CreateRenderer(window.get(), nullptr));
  if (!renderer) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateRenderer failed: %s", SDL_GetError());
    return nullptr;
  }

  // VSync and letterboxing are best-effort: the game still runs without them.
  const bool vsync = SDL_SetRenderVSync(renderer.get(), 1);
  if (!vsync) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "VSync refused, pacing manually: %s",
                SDL_GetError());
  }
  if (!SDL_SetRenderLogicalPresentation(renderer.get(), kCanvasWidth, kCanvasHeight,
                                        SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Logical presentation failed: %s", SDL_GetError());
  }
  SDL_SetWindowMinimumSize(window.get(), kCanvasWidth / 4, kCanvasHeight / 4);

  // (The Game inside App can throw if the physics world cannot be created;
  // main reports that.)
  std::unique_ptr<TableRenderer> table = TableRenderer::create(renderer.get(), assets);
  if (!table) {
    return nullptr;
  }

  std::unique_ptr<App> app(
      new App(options, window.release(), renderer.release(), vsync, std::move(table)));
  session.release();
  return app;
}

App::App(const AppOptions& options, SDL_Window* window, SDL_Renderer* renderer, bool vsync,
         std::unique_ptr<TableRenderer> table)
    : options_(options),
      window_(window),
      renderer_(renderer),
      table_(std::move(table)),
      vsync_(vsync),
      audio_(AudioOutput::open()),
      highScores_(loadHighScores()),
      aboutEvent_(SDL_RegisterEvents(1)) {
  const bool routed = routeAboutMenu(aboutEvent_);
  SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "About menu item routed to the game: %s",
               routed ? "yes" : "no");
}

App::~App() {
  table_.reset();
  renderer_.reset();
  window_.reset();
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

bool App::needsManualPacing() const {
  if (!vsync_) {
    return true;
  }
  const SDL_WindowFlags hidden =
      SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED;
  return (SDL_GetWindowFlags(window_.get()) & hidden) != 0;
}

namespace {
// How long a halted loop sleeps between redraws.
constexpr Sint32 kHaltedWaitMs = 100;
}  // namespace

int App::run() {
  std::uint64_t previous = SDL_GetTicksNS();
  std::uint32_t frames = 0;
  bool wasHalted = false;

  while (running_) {
    SDL_Event event;
    // While nothing is moving or sounding, sleep until something happens
    // instead of redrawing a still picture at full rate. A sound still
    // ringing keeps the loop at frame rate so its tail plays without gaps.
    if (halted() && !options_.frameLimit && !mixer_.busy() &&
        SDL_WaitEventTimeout(&event, kHaltedWaitMs)) {
      handleEvent(event);
    }
    while (SDL_PollEvent(&event)) {
      handleEvent(event);
    }

    const std::uint64_t frameStart = SDL_GetTicksNS();
    const double frameSeconds = static_cast<double>(frameStart - previous) / 1e9;
    previous = frameStart;

    readInput();

    // While halted the clock keeps moving but nothing accumulates. The first
    // frame after a halt adds no time either: its interval covers the wait,
    // and would otherwise arrive as a burst of catch-up steps.
    const bool isHalted = halted();
    if (!isHalted) {
      const int steps = timestep_.advance(wasHalted ? 0.0 : frameSeconds);
      // Sounds from every step of the frame start together, once each.
      SoundSet sounds = 0;
      for (int i = 0; i < steps; ++i) {
        game_.step(input_);
        sounds |= game_.sounds();
        // Checked per step, so a game that ends and restarts within one
        // frame is still recorded.
        recordFinishedGame();
        if (halted()) {
          // The initials prompt opened. Nothing more runs behind it, so a
          // key held this frame cannot start a new game under the prompt.
          break;
        }
      }
      mixer_.trigger(sounds);
      if (steps > 0) {
        // The simulation has seen every latched press at least once.
        latched_ = InputState{};
      }
    }
    wasHalted = isHalted;

    // The tune plays while the game runs and rests while it stands still.
    mixer_.setMusicPlaying(musicOn_ && !isHalted);
    if (audio_) {
      audio_->pump(mixer_);
    }

    // The accumulator is frozen while halted, so alpha is stable and nothing snaps.
    render(timestep_.alpha());

    // Presenting normally blocks until vblank. When it will not (no vsync,
    // hidden window) pace at the simulation rate. Otherwise still enforce a
    // floor, because a driver can accept vsync and then ignore it.
    const std::uint64_t minFrameNs = needsManualPacing() ? kUnsyncedFrameNs : kMinFrameNs;
    const std::uint64_t spent = SDL_GetTicksNS() - frameStart;
    if (spent < minFrameNs) {
      SDL_DelayPrecise(minFrameNs - spent);
    }

    ++frames;
    if (options_.frameLimit && frames >= *options_.frameLimit) {
      running_ = false;
    }
  }

  // Quitting at the initials prompt keeps the score, under the letters shown.
  commitInitials();

  // Self-check for automated runs: a long enough run must have simulated.
  if (options_.frameLimit && *options_.frameLimit >= 30 && game_.stepCount() == 0) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No simulation steps ran in %u frames",
                 static_cast<unsigned>(frames));
    return 1;
  }
  return 0;
}

void App::handleEvent(const SDL_Event& event) {
  switch (event.type) {
    case SDL_EVENT_QUIT:
      running_ = false;
      break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_RESTORED: {
      // Do not let the ball drain while the player is elsewhere. The window's
      // own state decides, not the event, so a restore without focus stays
      // paused. Timed automated runs have no player and never auto-pause.
      const SDL_WindowFlags flags = SDL_GetWindowFlags(window_.get());
      const bool away = (flags & SDL_WINDOW_INPUT_FOCUS) == 0 ||
                        (flags & SDL_WINDOW_MINIMIZED) != 0;
      autoPaused_ = away && !options_.frameLimit;
      latched_ = InputState{};
      break;
    }
    case SDL_EVENT_KEY_DOWN:
      if (event.key.repeat) {
        break;
      }
      if (entry_ && !aboutOpen_) {
        handleEntryKey(event.key.key, event.key.mod);
        break;
      }
      latchKey(event.key.scancode);
      if (event.key.key == SDLK_F1) {
        aboutOpen_ = !aboutOpen_;
        latched_ = InputState{};
      } else if (event.key.key == SDLK_ESCAPE && aboutOpen_) {
        aboutOpen_ = false;
        latched_ = InputState{};
      } else if (event.key.key == SDLK_F3) {
        paused_ = !paused_;
        // Nothing pressed during a pause carries into play.
        latched_ = InputState{};
      } else if (event.key.key == SDLK_F4) {
        toggleFullscreen();
      } else if (event.key.scancode == SDL_SCANCODE_M) {
        mixer_.setMuted(!mixer_.muted());
      } else if (event.key.scancode == SDL_SCANCODE_N) {
        musicOn_ = !musicOn_;
      }
      break;
    default:
      // The About item in the system menu bar. It only ever opens the screen.
      // Not over the initials prompt, which must be answered first, as with F1.
      if (aboutEvent_ != 0 && event.type == aboutEvent_ && !aboutOpen_ && !entry_) {
        aboutOpen_ = true;
        latched_ = InputState{};
      }
      break;
  }
}

namespace {

// Maps a physical key to the control it drives, or nullptr. Scancodes are
// key positions, so the controls sit under the same fingers on every
// keyboard layout. Positions match the original defaults.
bool InputState::* controlFor(SDL_Scancode scancode) noexcept {
  switch (scancode) {
    case SDL_SCANCODE_Z:
      return &InputState::leftFlipper;
    case SDL_SCANCODE_SLASH:
      return &InputState::rightFlipper;
    case SDL_SCANCODE_SPACE:
      return &InputState::plunger;
    case SDL_SCANCODE_X:
      return &InputState::nudgeLeft;
    case SDL_SCANCODE_PERIOD:
      return &InputState::nudgeRight;
    case SDL_SCANCODE_UP:
      return &InputState::nudgeUp;
    case SDL_SCANCODE_F2:
      return &InputState::newGame;
    default:
      return nullptr;
  }
}

constexpr SDL_Scancode kControlKeys[] = {
    SDL_SCANCODE_Z, SDL_SCANCODE_SLASH, SDL_SCANCODE_SPACE, SDL_SCANCODE_X,
    SDL_SCANCODE_PERIOD, SDL_SCANCODE_UP, SDL_SCANCODE_F2};

}  // namespace

void App::latchKey(SDL_Scancode scancode) {
  if (halted()) {
    return;
  }
  if (bool InputState::* const control = controlFor(scancode)) {
    latched_.*control = true;
  }
}

void App::readInput() {
  if (halted()) {
    input_ = InputState{};
    return;
  }
  int keyCount = 0;
  const bool* keys = SDL_GetKeyboardState(&keyCount);
  input_ = InputState{};
  for (const SDL_Scancode scancode : kControlKeys) {
    bool InputState::* const control = controlFor(scancode);
    const bool held = keys != nullptr && static_cast<int>(scancode) < keyCount &&
                      keys[static_cast<int>(scancode)];
    // Held now, or pressed and released since the last simulation step.
    input_.*control = held || latched_.*control;
  }
}

void App::handleEntryKey(SDL_Keycode key, SDL_Keymod modifiers) {
  if (!entry_) {
    return;
  }
  // The prompt appears the moment the last ball drains, while fingers are
  // still on the flipper keys. Presses in the first moments are play, not
  // typing. Shortcuts such as Cmd+H are not typing either.
  if (SDL_GetTicks() - entryOpenedMs_ < kEntryGuardMs ||
      (modifiers & (SDL_KMOD_GUI | SDL_KMOD_CTRL | SDL_KMOD_ALT)) != 0) {
    return;
  }
  // Letters are matched by what the key types, so they follow the layout.
  if (key >= SDLK_A && key <= SDLK_Z) {
    entry_->type(static_cast<char>('A' + static_cast<int>(key - SDLK_A)));
    return;
  }
  switch (key) {
    case SDLK_UP:
      entry_->next();
      break;
    case SDLK_DOWN:
      entry_->previous();
      break;
    case SDLK_LEFT:
    case SDLK_BACKSPACE:
      entry_->left();
      break;
    case SDLK_RIGHT:
      entry_->right();
      break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      // Not Space: it is the plunger key, and too easy to hit by habit.
      if (entry_->confirm()) {
        commitInitials();
      }
      break;
    case SDLK_ESCAPE:
      // Accept the letters as they stand.
      commitInitials();
      break;
    case SDLK_F4:
      toggleFullscreen();
      break;
    default:
      break;
  }
}

void App::commitInitials() {
  if (!entry_) {
    return;
  }
  pending_.initials = entry_->letters();
  lastInitials_ = pending_.initials;
  entry_.reset();
  latched_ = InputState{};
  newHighScore_ = highScores_.submit(pending_) < HighScores::kCapacity;
  if (newHighScore_) {
    saveHighScores(highScores_);
  }
}

void App::recordFinishedGame() {
  const bool over = game_.gameOver();
  if (over && !wasGameOver_) {
    // The game has just ended: enter it in the table and save if it made it.
    const HighScore entry{game_.rules().score(),
                          static_cast<std::uint8_t>(game_.rules().missions().rank())};
    if (!highScores_.qualifies(entry.score)) {
      newHighScore_ = false;
    } else if (options_.frameLimit) {
      // A timed automated run has no one to type: record it without initials.
      newHighScore_ = highScores_.submit(entry) < HighScores::kCapacity;
      if (newHighScore_) {
        saveHighScores(highScores_);
      }
    } else {
      // Ask for initials. The entry is made when they are confirmed.
      pending_ = entry;
      entry_.emplace(lastInitials_);
      entryOpenedMs_ = SDL_GetTicks();
      latched_ = InputState{};
    }
  } else if (!over) {
    newHighScore_ = false;
  }
  wasGameOver_ = over;
}

void App::toggleFullscreen() {
  // Ask the window, not a cached flag: the user can also change fullscreen
  // through the window manager.
  const bool isFullscreen = (SDL_GetWindowFlags(window_.get()) & SDL_WINDOW_FULLSCREEN) != 0;
  if (!SDL_SetWindowFullscreen(window_.get(), !isFullscreen)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Fullscreen toggle failed: %s", SDL_GetError());
  }
}

void App::render(double /*alpha*/) {
  SDL_Renderer* r = renderer_.get();

  // Letterbox bars.
  SDL_SetRenderDrawColor(r, 0, 0, 0, SDL_ALPHA_OPAQUE);
  SDL_RenderClear(r);

  const bool showPaused = paused_ || autoPaused_;
  table_->draw(r, game_, showPaused, HudExtras{highScores_.best(), newHighScore_});
  if (showPaused && !aboutOpen_ && !entry_) {
    drawText(r, "PAUSED", 384.0F, 430.0F, 9.0F, SDL_Color{235, 240, 255, 255}, TextAlign::Center);
    drawText(r, paused_ ? "PRESS F3 TO RESUME" : "CLICK THE WINDOW TO RESUME", 384.0F, 520.0F, 3.0F,
             SDL_Color{255, 190, 70, 255}, TextAlign::Center);
  }
  if (aboutOpen_) {
    drawAbout(r, highScores_);
  } else if (entry_) {
    drawInitialsEntry(r, *entry_, pending_.score, (SDL_GetTicks() / kCursorBlinkMs) % 2U == 0U);
  }

  SDL_RenderPresent(r);
}

}  // namespace pinball
