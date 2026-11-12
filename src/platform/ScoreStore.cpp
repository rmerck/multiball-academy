#include "platform/ScoreStore.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "platform/SafeFile.hpp"

namespace pinball {

namespace {

constexpr const char* kOrganisation = "space-cadet-pinball";
constexpr const char* kApplication = "Space Cadet Pinball";
constexpr const char* kFileName = "highscores.txt";

// Full path of the score file, or empty if there is no preferences directory.
std::string scoreFilePath() {
  // Created if missing; the returned string is ours to free.
  char* const directory = SDL_GetPrefPath(kOrganisation, kApplication);
  if (directory == nullptr) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No preferences directory: %s", SDL_GetError());
    return {};
  }
  std::string path(directory);
  SDL_free(directory);
  path += kFileName;
  return path;
}

}  // namespace

HighScores loadHighScores() {
  HighScores scores;
  const std::string path = scoreFilePath();
  if (path.empty()) {
    return scores;
  }

  std::vector<std::uint8_t> bytes;
  const ReadResult result = readRegularFile(path, HighScores::kMaxTextBytes, bytes);
  if (result == ReadResult::Missing) {
    return scores;  // Not there yet: first run.
  }
  if (result == ReadResult::Error) {
    // Unreadable right now. Leave it alone: it may be fine next time.
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not read the high score file");
    return scores;
  }
  const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  if (result != ReadResult::Ok || !scores.parse(text)) {
    // Keep the unusable file for inspection instead of overwriting it at
    // the next save. It may come from a newer version.
    const std::string rejected = path + ".bad";
    SDL_RemovePath(rejected.c_str());
    SDL_RenamePath(path.c_str(), rejected.c_str());
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "High score file is not valid; set aside as %s",
                rejected.c_str());
    return HighScores{};
  }
  return scores;
}

bool saveHighScores(const HighScores& scores) {
  const std::string path = scoreFilePath();
  if (path.empty()) {
    return false;
  }
  if (!replaceFile(path, scores.serialize())) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not save high scores");
    return false;
  }
  return true;
}

}  // namespace pinball
