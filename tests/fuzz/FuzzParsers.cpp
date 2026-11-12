// libFuzzer harness for every parser of outside input. The first byte picks
// the parser; the rest is its input. Each branch also checks what the parser
// promises about anything it accepts.
//
// Build with clang: -DPINBALL_FUZZ=ON -DPINBALL_SANITIZE=ON, then run
// build/pinball_fuzz -max_total_time=60
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "core/Args.hpp"
#include "core/AssetName.hpp"
#include "core/HighScores.hpp"
#include "core/PngHeader.hpp"
#include "core/Rules.hpp"

namespace {

void require(bool ok) {
  if (!ok) {
    std::abort();
  }
}

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
  // Every part between slashes is a real name: not empty, not "." or "..",
  // not hidden. (Two dots inside a name, as in "a..png", are harmless.)
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
  require(name.find('\\') == std::string_view::npos);
  require(name.find('\0') == std::string_view::npos);
  require(name.find(':') == std::string_view::npos);
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
  switch (which) {
    case 0:
      fuzzHighScores(text);
      break;
    case 1: {
      const auto png = pinball::parsePngHeader(
          reinterpret_cast<const unsigned char*>(text.data()), text.size());
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
