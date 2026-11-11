#include <SDL3/SDL_main.h>

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "platform/App.hpp"

namespace {

constexpr std::uint32_t kMaxFrameLimit = 1'000'000;

void printUsage() {
  std::fputs("Usage: pinball [--frames N]\n"
             "  --frames N   exit after N frames (1..1000000), for automated tests\n",
             stderr);
}

// Parses a strictly decimal value in [1, kMaxFrameLimit]. Rejects signs,
// whitespace, trailing characters and out-of-range values.
bool parseFrameLimit(std::string_view text, std::uint32_t& out) {
  if (text.empty()) {
    return false;
  }
  std::uint32_t value = 0;
  const char* const end = text.data() + text.size();
  const auto result = std::from_chars(text.data(), end, value, 10);
  if (result.ec != std::errc{} || result.ptr != end) {
    return false;
  }
  if (value < 1 || value > kMaxFrameLimit) {
    return false;
  }
  out = value;
  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  pinball::AppOptions options;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--frames" && i + 1 < argc) {
      std::uint32_t limit = 0;
      if (!parseFrameLimit(argv[++i], limit)) {
        printUsage();
        return EXIT_FAILURE;
      }
      options.frameLimit = limit;
    } else {
      printUsage();
      return EXIT_FAILURE;
    }
  }

  const auto app = pinball::App::create(options);
  if (!app) {
    return EXIT_FAILURE;
  }
  return app->run();
}
