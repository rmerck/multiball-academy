// TEMPORARY. The iOS entry point belongs in src/main_ios.cpp, which is not
// written yet. Until it is, cmake/Ios.cmake compiles this file so that the
// iOS application links and starts; it stops doing so by itself as soon as
// src/main_ios.cpp defines SDL_AppInit, and this file is then deleted.
//
// It does the least that runs the game: SDL's main callbacks around
// App::frame(), with the desktop picture. No touch input, no life cycle
// handling, no audio session setup: those come with the real entry point.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <exception>
#include <memory>
#include <string_view>

#include "core/Args.hpp"
#include "platform/App.hpp"

namespace {

std::unique_ptr<pinball::App> app;

}  // namespace

SDL_AppResult SDL_AppInit(void** /*state*/, int argc, char* argv[]) {
  if (app) {
    // The system can connect the scene a second time.
    return SDL_APP_CONTINUE;
  }
  pinball::AppOptions options;
  // "--frames N" as on the desktop, so a simulator run can end by itself.
  // Anything else is ignored: the system adds arguments of its own.
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string_view(argv[i]) == "--frames") {
      options.frameLimit = pinball::parseFrameLimit(argv[i + 1]);
      if (!options.frameLimit) {
        return SDL_APP_FAILURE;
      }
    }
  }
  try {
    app = pinball::App::create(options);
  } catch (const std::exception& e) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal: %s", e.what());
  } catch (...) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal: unknown error");
  }
  return app ? SDL_APP_CONTINUE : SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppEvent(void* /*state*/, SDL_Event* event) {
  if (app && event != nullptr) {
    app->handleEvent(*event);
  }
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* /*state*/) {
  if (!app) {
    return SDL_APP_FAILURE;
  }
  try {
    if (app->frame() == pinball::FrameResult::Continue) {
      return SDL_APP_CONTINUE;
    }
    const int code = app->finish();
    SDL_Log("finished: exit code %d", code);
    return code == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
  } catch (const std::exception& e) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal: %s", e.what());
  } catch (...) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal: unknown error");
  }
  return SDL_APP_FAILURE;
}

void SDL_AppQuit(void* /*state*/, SDL_AppResult /*result*/) { app.reset(); }
