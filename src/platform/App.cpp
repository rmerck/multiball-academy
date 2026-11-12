#include "platform/App.hpp"

#include <cmath>
#include <cstddef>
#include <utility>

#include "core/TableLayout.hpp"

namespace pinball {

namespace {

// Frame period used when presenting does not block (no vsync, hidden window).
constexpr std::uint64_t kUnsyncedFrameNs = 1'000'000'000ULL / 120ULL;
// Hard floor on frame time that applies even when vsync is reported. 1000 FPS
// is above any display refresh rate, so it never costs a vblank, and it still
// stops a driver that ignores vsync from spinning a core flat out.
constexpr std::uint64_t kMinFrameNs = 1'000'000'000ULL / 1000ULL;

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
  SDL_SetAppMetadata("Space Cadet Pinball", "0.1.0", "io.github.rmerck.spacecadetpinball");

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
      SDL_CreateWindow("Space Cadet Pinball", kLogicalWidth, kLogicalHeight,
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
  if (!SDL_SetRenderLogicalPresentation(renderer.get(), kLogicalWidth, kLogicalHeight,
                                        SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Logical presentation failed: %s", SDL_GetError());
  }
  SDL_SetWindowMinimumSize(window.get(), kLogicalWidth / 4, kLogicalHeight / 4);

  // A missing or damaged asset is fatal: fail at startup, not mid-game.
  // (The Game inside App can throw if the physics world cannot be created;
  // main reports that.)
  Textures textures;
  textures.ball = assets.loadTexture(renderer.get(), "sprites/ball.png");
  if (!textures.ball) {
    return nullptr;
  }

  std::unique_ptr<App> app(
      new App(options, window.release(), renderer.release(), vsync, std::move(textures)));
  session.release();
  return app;
}

App::App(const AppOptions& options, SDL_Window* window, SDL_Renderer* renderer, bool vsync,
         Textures textures)
    : options_(options),
      window_(window),
      renderer_(renderer),
      textures_(std::move(textures)),
      vsync_(vsync) {
  // Built once: the shell outline as a closed strip for debug drawing.
  const std::vector<Vec2> shell = table::outerBoundary();
  outline_.reserve(shell.size() + 1U);
  for (const Vec2 p : shell) {
    outline_.push_back({p.x, p.y});
  }
  if (!shell.empty()) {
    outline_.push_back({shell.front().x, shell.front().y});
  }
}

App::~App() {
  textures_ = Textures{};
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

int App::run() {
  std::uint64_t previous = SDL_GetTicksNS();
  std::uint32_t frames = 0;

  while (running_) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      handleEvent(event);
    }

    const std::uint64_t frameStart = SDL_GetTicksNS();
    const double frameSeconds = static_cast<double>(frameStart - previous) / 1e9;
    previous = frameStart;

    readInput();

    // While paused the clock keeps moving but nothing accumulates, so there is
    // no burst of catch-up steps on resume.
    if (!paused_) {
      const int steps = timestep_.advance(frameSeconds);
      for (int i = 0; i < steps; ++i) {
        game_.step(input_);
      }
      if (steps > 0) {
        // The simulation has seen every latched press at least once.
        latched_ = InputState{};
      }
    }

    // The accumulator is frozen while paused, so alpha is stable and nothing snaps.
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
    case SDL_EVENT_KEY_DOWN:
      if (event.key.repeat) {
        break;
      }
      latchKey(event.key.scancode);
      if (event.key.key == SDLK_F3) {
        paused_ = !paused_;
        // Nothing pressed during a pause carries into play.
        latched_ = InputState{};
      } else if (event.key.key == SDLK_F4) {
        toggleFullscreen();
      }
      break;
    default:
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
  if (paused_) {
    return;
  }
  if (bool InputState::* const control = controlFor(scancode)) {
    latched_.*control = true;
  }
}

void App::readInput() {
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

void App::toggleFullscreen() {
  // Ask the window, not a cached flag: the user can also change fullscreen
  // through the window manager.
  const bool isFullscreen = (SDL_GetWindowFlags(window_.get()) & SDL_WINDOW_FULLSCREEN) != 0;
  if (!SDL_SetWindowFullscreen(window_.get(), !isFullscreen)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Fullscreen toggle failed: %s", SDL_GetError());
  }
}

void App::drawTableOutline() {
  SDL_Renderer* r = renderer_.get();
  SDL_SetRenderDrawColor(r, 120, 150, 230, SDL_ALPHA_OPAQUE);

  // Outer shell, closed.
  if (outline_.size() >= 2) {
    SDL_RenderLines(r, outline_.data(), static_cast<int>(outline_.size()));
  }
  // Interior walls as their centre lines.
  for (const table::Capsule& wall : table::kWalls) {
    SDL_RenderLine(r, wall.a.x, wall.a.y, wall.b.x, wall.b.y);
  }
  for (const table::Triangle& sling : table::kSlingshots) {
    for (std::size_t i = 0; i < 3; ++i) {
      const Vec2 a = sling.points[i];
      const Vec2 b = sling.points[(i + 1) % 3];
      SDL_RenderLine(r, a.x, a.y, b.x, b.y);
    }
  }
}

void App::render(double /*alpha*/) {
  SDL_Renderer* r = renderer_.get();

  // Letterbox bars.
  SDL_SetRenderDrawColor(r, 0, 0, 0, SDL_ALPHA_OPAQUE);
  SDL_RenderClear(r);

  // Playfield on the left at 1:1 with table coordinates, info panel on the right.
  const SDL_FRect playfield{0.0F, 0.0F, table::kWidth, table::kHeight};
  const SDL_FRect panel{table::kWidth, 0.0F, static_cast<float>(kLogicalWidth) - table::kWidth,
                        static_cast<float>(kLogicalHeight)};
  SDL_SetRenderDrawColor(r, 10, 14, 40, SDL_ALPHA_OPAQUE);
  SDL_RenderFillRect(r, &playfield);

  // Placeholder outline until the table art lands.
  drawTableOutline();

  // Plunger block and, when closed, the lane gate.
  const SDL_FRect plunger{table::kLaneCenterX - table::kPlungerWidth * 0.5F,
                          table::kPlungerRestTopY + game_.plungerPull(), table::kPlungerWidth,
                          table::kPlungerHeight};
  SDL_SetRenderDrawColor(r, 170, 170, 180, SDL_ALPHA_OPAQUE);
  SDL_RenderFillRect(r, &plunger);
  if (game_.laneGateClosed()) {
    SDL_RenderLine(r, table::kLaneGate.a.x, table::kLaneGate.a.y, table::kLaneGate.b.x,
                   table::kLaneGate.b.y);
  }

  // Flippers as thick placeholder lines along their centre lines.
  SDL_SetRenderDrawColor(r, 230, 120, 60, SDL_ALPHA_OPAQUE);
  for (std::size_t i = 0; i < table::kFlippers.size(); ++i) {
    const FlipperView flipper = game_.flipper(i);
    const float dx = std::cos(flipper.angle);
    const float dy = std::sin(flipper.angle);
    for (int offset = -3; offset <= 3; ++offset) {
      const float ox = -dy * static_cast<float>(offset);
      const float oy = dx * static_cast<float>(offset);
      SDL_RenderLine(r, flipper.pivot.x + ox, flipper.pivot.y + oy,
                     flipper.pivot.x + dx * table::kFlipperLength + ox,
                     flipper.pivot.y + dy * table::kFlipperLength + oy);
    }
  }

  const BallView ball = game_.ball();
  const float radius = table::kBallRadius;
  const SDL_FRect ballRect{ball.position.x - radius, ball.position.y - radius, radius * 2.0F,
                           radius * 2.0F};
  SDL_SetTextureAlphaMod(textures_.ball.get(), paused_ ? 128 : 255);
  SDL_RenderTexture(r, textures_.ball.get(), nullptr, &ballRect);

  // Drawn last so nothing on the playfield can spill over the panel.
  SDL_SetRenderDrawColor(r, 24, 24, 30, SDL_ALPHA_OPAQUE);
  SDL_RenderFillRect(r, &panel);

  SDL_RenderPresent(r);
}

}  // namespace pinball
