// The iOS entry point: SDL's main callbacks around App::frame(), with the
// application's life cycle events passed to App::handleEvent() as they
// happen. Built for iOS only, in place of main.cpp (CMakeLists.txt).
//
// On iOS the system owns the loop. SDL calls SDL_AppIterate once per
// display refresh from a display link, and SDL_AppEvent for every event:
// the life cycle ones at the moment the system sends them, when it may give
// the application no more time afterwards.
//
// How a run ends. A player cannot quit an iOS application, and this one
// never quits by itself. When SDL_AppIterate returns anything but
// "continue", SDL calls SDL_AppQuit and then exit() (SDL 3.4.20,
// src/main/ios/SDL_sysmain_callbacks.m). That happens in three cases only:
//   - the system is ending the application (SDL_EVENT_TERMINATING);
//   - the graphics device is lost for good, where the alternative is a dead
//     screen;
//   - a timed automated run, "--frames N" given as a launch argument (from
//     Xcode or simctl; there is no way to pass one from the home screen),
//     has drawn its frames. Launch arguments are honoured only in builds
//     for testing: debug builds, and any build compiled with
//     PINBALL_IOS_TEST_EXIT. A release build ignores them, so the build
//     that is submitted has no path that ends the process on purpose.
// The other launch argument, "--scene NAME", starts the game in a staged
// moment for screenshots (core/Scenes.hpp). It is read under the same rule.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <exception>
#include <memory>
#include <string_view>

#include "core/Args.hpp"
#include "platform/App.hpp"

namespace {

#if defined(PINBALL_IOS_TEST_EXIT) || !defined(NDEBUG)
constexpr bool kHonourArguments = true;
#else
constexpr bool kHonourArguments = false;
#endif

// The one application. Also the guard against SDL_AppInit running twice:
// UIKit can disconnect and reconnect the main scene, and SDL has been seen
// to run the entry point again when it does.
std::unique_ptr<pinball::App> gApp;
// Set once the last frame has been reported, so that it is reported once.
bool gFinished = false;

// Ends the run: keeps a score waiting at the initials prompt, and for a
// timed run logs the line an automated test looks for.
SDL_AppResult finishRun() {
  if (!gApp || gFinished) {
    return SDL_APP_SUCCESS;
  }
  gFinished = true;
  int code = 1;
  try {
    const pinball::App::Status status = gApp->status();
    code = gApp->finish();
    SDL_Log("frames=%u steps=%llu %s", static_cast<unsigned>(status.frames),
            static_cast<unsigned long long>(status.steps), code == 0 ? "ok" : "FAILED");
  } catch (const std::exception& e) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal: %s", e.what());
  } catch (...) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal: unknown error");
  }
  return code == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
}

}  // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
  if (appstate != nullptr) {
    *appstate = nullptr;
  }
  if (gApp) {
    // A second call with the application alive: nothing is made twice.
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_AppInit called again; ignored");
    return SDL_APP_CONTINUE;
  }

  pinball::AppOptions options;
  options.idiom = pinball::screen::Idiom::Touch;
  // Opening the audio device can wait many seconds for the audio session.
  // Not on the main thread, at launch, where the system would end the
  // application for it.
  options.backgroundAudioOpen = true;

  // The same arguments as on a desktop, so the same parser. What the system
  // or Xcode adds on its own (pairs such as -NSDocumentRevisionsDebugMode
  // YES) is skipped, never an error: there is nobody to read a usage text.
  if (kHonourArguments) {
    for (int i = 1; i < argc; ++i) {
      if (argv[i] != nullptr && std::string_view(argv[i]) == "--frames" && i + 1 < argc &&
          argv[i + 1] != nullptr) {
        options.frameLimit = pinball::parseFrameLimit(argv[++i]);
        if (!options.frameLimit) {
          SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "--frames needs a number from 1 to 1000000");
          return SDL_APP_FAILURE;
        }
      } else if (argv[i] != nullptr && std::string_view(argv[i]) == "--scene" && i + 1 < argc &&
                 argv[i + 1] != nullptr) {
        options.scene = pinball::parseScene(argv[++i]);
        if (!options.scene) {
          SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                       "--scene needs one of: table multiball hangar course graduation");
          return SDL_APP_FAILURE;
        }
      }
    }
  }

  try {
    gApp = pinball::App::create(options);
  } catch (const std::exception& e) {
    // No exception may leave a callback: SDL is C.
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal: %s", e.what());
  } catch (...) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal: unknown error");
  }
  gFinished = false;
  return gApp ? SDL_APP_CONTINUE : SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppIterate(void* /*appstate*/) {
  if (!gApp) {
    return SDL_APP_FAILURE;
  }
  try {
    if (gApp->frame() == pinball::FrameResult::Continue) {
      return SDL_APP_CONTINUE;
    }
  } catch (const std::exception& e) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal: %s", e.what());
    return SDL_APP_FAILURE;
  } catch (...) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal: unknown error");
    return SDL_APP_FAILURE;
  }
  return finishRun();
}

SDL_AppResult SDL_AppEvent(void* /*appstate*/, SDL_Event* event) {
  if (!gApp || event == nullptr) {
    return SDL_APP_CONTINUE;
  }
  try {
    // Everything goes through the one handler, the life cycle events too.
    // A score waiting at the initials prompt is kept there, at once, when
    // the application goes to the background or is ended.
    gApp->handleEvent(*event);
  } catch (const std::exception& e) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal: %s", e.what());
    return SDL_APP_FAILURE;
  } catch (...) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal: unknown error");
    return SDL_APP_FAILURE;
  }
  // Whether to stop is decided by the next SDL_AppIterate, from the
  // application's own state.
  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* /*appstate*/, SDL_AppResult /*result*/) {
  // Reached without a last frame when the system ends the application.
  (void)finishRun();
  try {
    gApp.reset();
  } catch (...) {
    // A destructor does not throw; SDL is C all the same.
  }
  // SDL shuts itself down after this returns.
}
