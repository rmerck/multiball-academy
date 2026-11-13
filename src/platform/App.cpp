#include "platform/App.hpp"

#include <cstdint>
#include <memory>
#include <utility>

#include "core/LoopPolicy.hpp"
#include "core/Version.hpp"
#include "platform/About.hpp"
#include "platform/Assets.hpp"
#include "platform/InitialsScreen.hpp"
#include "platform/MacMenu.hpp"
#include "platform/PauseMenu.hpp"

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
  SDL_SetAppMetadata(kGameTitle, kGameVersion, kGameIdentifier);
  SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_URL_STRING, "https://github.com/rmerck");

  // Refuse to start unless every asset matches the build. Done first, before
  // any window exists.
  AssetLoader assets = AssetLoader::fromExecutableLocation();
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

  // The desktop's canvas is fixed, and the window opens at its size. (A
  // touch layout will follow the window instead; until that is written
  // every idiom gets this one.)
  const screen::Layout layout = screen::desktopLayout();
  const int canvasWidth = static_cast<int>(layout.canvasWidth);
  const int canvasHeight = static_cast<int>(layout.canvasHeight);

  std::unique_ptr<SDL_Window, WindowDeleter> window(
      SDL_CreateWindow(kGameTitle, canvasWidth, canvasHeight,
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
  if (!SDL_SetRenderLogicalPresentation(renderer.get(), canvasWidth, canvasHeight,
                                        SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Logical presentation failed: %s", SDL_GetError());
  }
  SDL_SetWindowMinimumSize(window.get(), canvasWidth / 4, canvasHeight / 4);

  // (The Game inside App can throw if the physics world cannot be created;
  // main reports that.)
  std::unique_ptr<TableRenderer> table =
      TableRenderer::create(renderer.get(), assets, TextureSet::Desktop);
  if (!table) {
    return nullptr;
  }

  std::unique_ptr<App> app(
      new App(options, layout, window.release(), renderer.release(), vsync, std::move(table)));
  session.release();
  return app;
}

App::App(const AppOptions& options, const screen::Layout& layout, SDL_Window* window,
         SDL_Renderer* renderer, bool vsync, std::unique_ptr<TableRenderer> table)
    : options_(options),
      layout_(layout),
      window_(window),
      renderer_(renderer),
      table_(std::move(table)),
      previousNs_(SDL_GetTicksNS()),
      vsync_(vsync),
      audio_(AudioOutput::open()),
      // A timed automated run leaves the player's score file alone: it is
      // neither read (which can also set a bad file aside) nor written.
      highScores_(options.frameLimit ? HighScores{} : scoreStore_.load()),
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
  if (!vsync_ || !presented_) {
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
  previousNs_ = SDL_GetTicksNS();

  FrameResult result = FrameResult::Continue;
  while (result == FrameResult::Continue) {
    // While nothing is moving or sounding, sleep until something happens
    // instead of redrawing a still picture at full rate. A sound still
    // ringing keeps the loop at frame rate so its tail plays without gaps.
    // It sleeps anyway after a while, in case the audio device has stopped
    // taking sound and a tail can never finish. With no audio device nothing
    // drains the mixer, so its voices never end and do not count.
    haltedFrames_ = halted() ? haltedFrames_ + 1 : 0;
    SDL_Event event;
    if (sleepsWhileHalted(halted(), options_.frameLimit.has_value(),
                          audio_ != nullptr && mixer_.busy(), haltedFrames_) &&
        SDL_WaitEventTimeout(&event, kHaltedWaitMs)) {
      handleEvent(event);
    }

    result = frame();

    // Presenting normally blocks until vblank. When it will not (no vsync,
    // hidden window) pace at the simulation rate. Otherwise still enforce a
    // floor, because a driver can accept vsync and then ignore it.
    const std::uint64_t minFrameNs = needsManualPacing() ? kUnsyncedFrameNs : kMinFrameNs;
    const std::uint64_t spent = SDL_GetTicksNS() - frameStartNs_;
    if (spent < minFrameNs) {
      SDL_DelayPrecise(minFrameNs - spent);
    }
  }
  return finish();
}

FrameResult App::frame() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    handleEvent(event);
  }

  frameStartNs_ = SDL_GetTicksNS();
  const double frameSeconds = static_cast<double>(frameStartNs_ - previousNs_) / 1e9;
  previousNs_ = frameStartNs_;

  readInput();

  // While halted the clock keeps moving but nothing accumulates, and the
  // first frame after a halt adds no time either (see PauseClock).
  const bool isHalted = halted();
  const double simSeconds = clock_.simulated(frameSeconds, isHalted);
  if (!isHalted) {
    const int steps = timestep_.advance(simSeconds);
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

  // The tune plays while the game runs and rests while it stands still.
  mixer_.setMusicPlaying(musicOn_ && !isHalted);
  if (audio_) {
    audio_->pump(mixer_);
  }

  // The accumulator is frozen while halted, so alpha is stable and nothing snaps.
  render(timestep_.alpha());

  ++frames_;
  if (options_.frameLimit && frames_ >= *options_.frameLimit) {
    running_ = false;
  }
  return running_ ? FrameResult::Continue : FrameResult::Quit;
}

int App::finish() {
  // Quitting at the initials prompt keeps the score, under the letters shown.
  commitInitials();

  if (deviceLost_) {
    return 1;
  }

  // Self-check for automated runs: a long enough run must have simulated.
  if (options_.frameLimit && *options_.frameLimit >= 30 && game_.stepCount() == 0) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No simulation steps ran in %u frames",
                 static_cast<unsigned>(frames_));
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
    case SDL_EVENT_WINDOW_RESTORED:
      handleFocusChange();
      break;
    case SDL_EVENT_RENDER_DEVICE_RESET:
      // Every texture died with the old device. (Render targets, which have
      // their own event, are not used.)
      reloadTextures();
      break;
    case SDL_EVENT_RENDER_DEVICE_LOST:
      // Nothing can be drawn again. Leave through the normal exit, so a score
      // waiting at the initials prompt is still kept.
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "The graphics device was lost; exiting");
      deviceLost_ = true;
      running_ = false;
      break;
    case SDL_EVENT_KEY_DOWN:
      handleKeyDown(event.key);
      break;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
      handleTouch(event);
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
    scoreStore_.save(highScores_);
  }
}

void App::recordFinishedGame() {
  const bool over = game_.gameOver();
  if (over && !wasGameOver_) {
    // The game has just ended: enter it in the table if it made it.
    const HighScore entry{game_.rules().score(),
                          static_cast<std::uint8_t>(game_.rules().missions().rank())};
    if (!highScores_.qualifies(entry.score)) {
      newHighScore_ = false;
    } else if (options_.frameLimit) {
      // A timed automated run has no one to type: enter it without initials,
      // for this run only. Nothing is saved.
      newHighScore_ = highScores_.submit(entry) < HighScores::kCapacity;
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

void App::noteRenderFailure(const char* call) {
  // Once: a renderer that fails tends to fail every frame. The game keeps
  // running, since a device reset or a restored window can bring it back.
  if (!renderFailureLogged_) {
    renderFailureLogged_ = true;
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s failed: %s", call, SDL_GetError());
  }
}

void App::reloadTextures() {
  // Reading, checking and decoding every asset takes many frames' worth of
  // time, inside event handling. Count it as a halt, so the frame that
  // follows adds no simulation time instead of a burst of catch-up steps.
  // Whether or not the reload works: the time is spent either way.
  (void)clock_.simulated(0.0, true);
  // Read and verified again from disk: the bytes checked at start were freed.
  AssetLoader assets = AssetLoader::fromExecutableLocation();
  std::unique_ptr<TableRenderer> table;
  if (assets.verifyAll()) {
    table = TableRenderer::create(renderer_.get(), assets, TextureSet::Desktop);
  }
  if (!table) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Could not reload the textures after a graphics device reset");
    return;
  }
  table_ = std::move(table);
}

void App::render(double alpha) {
  SDL_Renderer* r = renderer_.get();

  // Letterbox bars, and whatever shows at the table's edge when a nudge
  // jolts it.
  SDL_SetRenderDrawColor(r, 0, 0, 0, SDL_ALPHA_OPAQUE);
  if (!SDL_RenderClear(r)) {
    noteRenderFailure("SDL_RenderClear");
  }

  const bool showPaused = paused_ || autoPaused_;
  // The ball and flippers are drawn part-way between the last two steps.
  table_->drawBackdrop(r, layout_);
  table_->draw(r, game_, static_cast<float>(alpha), layout_, DrawOptions{.dimmed = showPaused},
               HudExtras{highScores_.best(), newHighScore_});
  if (showPaused && !aboutOpen_ && !entry_) {
    drawPauseMenu(r, layout_,
                  MenuState{.muted = mixer_.muted(),
                            .musicOn = musicOn_,
                            .confirmNewGame = false,
                            .afterInterruption = !paused_});
  }
  if (aboutOpen_) {
    drawAbout(r, highScores_, layout_, AboutState{});
  } else if (entry_) {
    drawInitialsEntry(r, *entry_, pending_.score, (SDL_GetTicks() / kCursorBlinkMs) % 2U == 0U,
                      layout_);
  }

  presented_ = SDL_RenderPresent(r);
  if (!presented_) {
    noteRenderFailure("SDL_RenderPresent");
  }
}

}  // namespace pinball
