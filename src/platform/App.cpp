#include "platform/App.hpp"

#include <cmath>

namespace pinball {

namespace {

// Guards SDL_Init/SDL_Quit so SDL is shut down on every exit path of create().
class SdlSession {
 public:
  SdlSession() : ok_(SDL_Init(SDL_INIT_VIDEO)) {}
  ~SdlSession() {
    if (ok_ && !released_) {
      SDL_Quit();
    }
  }
  SdlSession(const SdlSession&) = delete;
  SdlSession& operator=(const SdlSession&) = delete;
  bool ok() const noexcept { return ok_; }
  // Hands responsibility for SDL_Quit to the App.
  void release() noexcept { released_ = true; }

 private:
  bool ok_;
  bool released_ = false;
};

}  // namespace

std::unique_ptr<App> App::create(const AppOptions& options) {
  SDL_SetAppMetadata("Space Cadet Pinball", "0.1.0", "io.github.rmerck.spacecadetpinball");

  SdlSession session;
  if (!session.ok()) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
    return nullptr;
  }

  SDL_Window* window = SDL_CreateWindow("Space Cadet Pinball", kLogicalWidth, kLogicalHeight,
                                        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (window == nullptr) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed: %s", SDL_GetError());
    return nullptr;
  }
  std::unique_ptr<SDL_Window, WindowDeleter> windowGuard(window);

  SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
  if (renderer == nullptr) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateRenderer failed: %s", SDL_GetError());
    return nullptr;
  }
  std::unique_ptr<SDL_Renderer, RendererDeleter> rendererGuard(renderer);

  // VSync and letterboxing are best-effort: the game still runs without them.
  if (!SDL_SetRenderVSync(renderer, 1)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "VSync unavailable: %s", SDL_GetError());
  }
  if (!SDL_SetRenderLogicalPresentation(renderer, kLogicalWidth, kLogicalHeight,
                                        SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Logical presentation failed: %s", SDL_GetError());
  }
  SDL_SetWindowMinimumSize(window, kLogicalWidth / 4, kLogicalHeight / 4);

  std::unique_ptr<App> app(new App(options, windowGuard.release(), rendererGuard.release()));
  session.release();
  return app;
}

App::App(const AppOptions& options, SDL_Window* window, SDL_Renderer* renderer)
    : options_(options), window_(window), renderer_(renderer) {}

App::~App() {
  renderer_.reset();
  window_.reset();
  SDL_Quit();
}

int App::run() {
  std::uint64_t previous = SDL_GetTicksNS();
  std::uint32_t frames = 0;

  while (running_) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      handleEvent(event);
    }

    const std::uint64_t now = SDL_GetTicksNS();
    const double frameSeconds = static_cast<double>(now - previous) / 1e9;
    previous = now;

    if (!paused_) {
      const int steps = timestep_.advance(frameSeconds);
      for (int i = 0; i < steps; ++i) {
        update(timestep_.step());
      }
    }

    render(paused_ ? 0.0 : timestep_.alpha());

    ++frames;
    if (options_.frameLimit && frames >= *options_.frameLimit) {
      running_ = false;
    }
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
      if (event.key.key == SDLK_F3) {
        paused_ = !paused_;
      } else if (event.key.key == SDLK_F4) {
        toggleFullscreen();
      }
      break;
    default:
      break;
  }
}

void App::toggleFullscreen() {
  const bool target = !fullscreen_;
  if (SDL_SetWindowFullscreen(window_.get(), target)) {
    fullscreen_ = target;
  } else {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Fullscreen toggle failed: %s", SDL_GetError());
  }
}

void App::update(double dt) {
  simTime_ += dt;
}

void App::render(double /*alpha*/) {
  SDL_Renderer* r = renderer_.get();

  // Letterbox bars.
  SDL_SetRenderDrawColor(r, 0, 0, 0, SDL_ALPHA_OPAQUE);
  SDL_RenderClear(r);

  // Placeholder layout: playfield on the left, info panel on the right.
  const float w = static_cast<float>(kLogicalWidth);
  const float h = static_cast<float>(kLogicalHeight);
  const SDL_FRect playfield{0.0F, 0.0F, w * 0.6F, h};
  const SDL_FRect panel{w * 0.6F, 0.0F, w * 0.4F, h};
  SDL_SetRenderDrawColor(r, 10, 14, 40, SDL_ALPHA_OPAQUE);
  SDL_RenderFillRect(r, &playfield);
  SDL_SetRenderDrawColor(r, 24, 24, 30, SDL_ALPHA_OPAQUE);
  SDL_RenderFillRect(r, &panel);

  // A marker that moves with simulated time, proving the fixed-step loop runs.
  const float t = static_cast<float>(std::fmod(simTime_, 2.0) / 2.0);
  const SDL_FRect marker{playfield.w * t - 8.0F, h * 0.5F - 8.0F, 16.0F, 16.0F};
  if (paused_) {
    SDL_SetRenderDrawColor(r, 200, 160, 40, SDL_ALPHA_OPAQUE);
  } else {
    SDL_SetRenderDrawColor(r, 200, 200, 210, SDL_ALPHA_OPAQUE);
  }
  SDL_RenderFillRect(r, &marker);

  SDL_RenderPresent(r);
}

}  // namespace pinball
