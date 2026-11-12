#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "core/Args.hpp"
#include "platform/App.hpp"

namespace {

void printUsage() {
  std::fputs("Usage: pinball [--frames N]\n"
             "  --frames N   exit after N frames (1..1000000), for automated tests\n",
             stderr);
}

}  // namespace

int main(int argc, char* argv[]) {
  pinball::AppOptions options;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--frames" && i + 1 < argc) {
      options.frameLimit = pinball::parseFrameLimit(argv[++i]);
      if (!options.frameLimit) {
        printUsage();
        return EXIT_FAILURE;
      }
    } else {
      printUsage();
      return EXIT_FAILURE;
    }
  }

  int exitCode = EXIT_FAILURE;
  {
    const auto app = pinball::App::create(options);
    if (app) {
      exitCode = app->run();
    }
  }
  SDL_Quit();
  return exitCode;
}
