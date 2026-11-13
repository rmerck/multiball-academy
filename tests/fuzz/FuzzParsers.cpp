// libFuzzer harness for every parser of outside input. The first byte picks
// the parser; the rest is its input. Each branch also checks what the parser
// promises about anything it accepts.
//
// Build with clang: -DPINBALL_FUZZ=ON -DPINBALL_SANITIZE=ON, then run
// build/pinball_fuzz -max_total_time=60 -dict=tests/fuzz/parsers.dict <new dir> tests/fuzz/corpus
//
// Seeds in tests/fuzz/corpus start with '0', '1', '2' or '3', which select
// the score, PNG, asset name and argument parsers in that order, or with
// '4' for the review tools' window description.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>

#include "core/Args.hpp"
#include "core/AssetName.hpp"
#include "core/HighScores.hpp"
#include "core/PngHeader.hpp"
#include "core/Rules.hpp"
#include "core/SurfaceSpec.hpp"

namespace {

// Says which promise was broken before stopping, so a crash report names it.
void requireAt(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "invariant broken at line %d: %s\n", line, what);
    std::abort();
  }
}
#define require(expr) requireAt((expr), #expr, __LINE__)

void fuzzHighScores(std::string_view text) {
  pinball::HighScores scores;
  if (!scores.parse(text)) {
    require(scores.size() == 0);
    return;
  }
  require(text.size() <= pinball::HighScores::kMaxTextBytes);
  require(scores.size() <= pinball::HighScores::kCapacity);
  for (std::size_t i = 0; i < scores.size(); ++i) {
    const pinball::HighScore entry = scores.at(i);
    require(entry.score > 0 && entry.score <= pinball::points::kMaxScore);
    require(i == 0 || scores.at(i - 1).score >= entry.score);
    for (const char c : entry.initials) {
      require(pinball::isInitialsChar(c));
    }
  }
  // What it writes, it reads back the same.
  const std::string written = scores.serialize();
  require(written.size() <= pinball::HighScores::kMaxTextBytes);
  pinball::HighScores again;
  require(again.parse(written));
  require(again.serialize() == written);
}

void fuzzAssetName(std::string_view name) {
  if (!pinball::isSafeAssetName(name)) {
    return;
  }
  require(!name.empty() && name.size() <= 128);
  require(!name.contains(".."));
  // Every part between slashes is a real name: not empty and not hidden.
  std::size_t start = 0;
  while (start <= name.size()) {
    std::size_t end = name.find('/', start);
    if (end == std::string_view::npos) {
      end = name.size();
    }
    const std::string_view part = name.substr(start, end - start);
    require(!part.empty() && part.front() != '.' && part.back() != '.');
    start = end + 1;
  }
  require(!name.contains('\\'));
  require(!name.contains('\0'));
  require(!name.contains(':'));
}

// The window description of the review tools (core/SurfaceSpec.hpp). The
// parser is in a header and is given its presets, so it needs nothing that
// is not already linked here.
void fuzzSurfaceSpec(std::string_view text) {
  namespace screen = pinball::screen;
  static constexpr screen::Preset kKnown[] = {
      {"desktop", {1280.0F, 960.0F, 0.0F, 0.0F, 0.0F, 0.0F}, screen::Idiom::Desktop},
      {"phone", {393.0F, 852.0F, 59.0F, 34.0F, 0.0F, 0.0F}, screen::Idiom::Touch},
  };
  const auto spec = screen::parseSurfaceSpec(text, kKnown);
  if (!spec) {
    return;
  }
  require(!text.empty() && text.size() <= 64);
  require(spec->scale >= 0 && spec->scale <= static_cast<int>(screen::kMaxSpecScale));
  const screen::Surface& s = spec->surface;
  require(s.width >= static_cast<float>(screen::kMinSpecPoints) &&
          s.width <= static_cast<float>(screen::kMaxSpecPoints));
  require(s.height >= static_cast<float>(screen::kMinSpecPoints) &&
          s.height <= static_cast<float>(screen::kMaxSpecPoints));
  require(s.safeTop >= 0.0F && s.safeBottom >= 0.0F && s.safeLeft >= 0.0F && s.safeRight >= 0.0F);
  require(s.safeTop + s.safeBottom <= s.height * screen::kMaxInsetShare);
  require(s.safeLeft + s.safeRight <= s.width * screen::kMaxInsetShare);
  // Only these characters can be part of one.
  for (const char c : text) {
    require((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == 'x' || c == '@' ||
            c == '+' || c == ',' || c == '-');
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size == 0) {
    return 0;
  }
  const std::uint8_t which = data[0] % 4U;
  const std::uint8_t* const body = data + 1;
  const std::size_t length = size - 1;
  // A copy of exactly the input's size, so any read past it is caught.
  const std::string text(reinterpret_cast<const char*>(body), length);
  if (data[0] == '4') {
    fuzzSurfaceSpec(text);
    return 0;
  }
  switch (which) {
    case 0:
      fuzzHighScores(text);
      break;
    case 1: {
      const auto png = pinball::parsePngHeader(
          std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
      require(!png || (text.size() >= pinball::kPngHeaderBytes && png->width > 0 &&
                       png->height > 0));
      break;
    }
    case 2:
      fuzzAssetName(text);
      break;
    default: {
      const auto frames = pinball::parseFrameLimit(text);
      require(!frames || (*frames >= 1 && *frames <= pinball::kMaxFrameLimit));
      break;
    }
  }
  return 0;
}
