#include "platform/ScoreStore.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <string>
#include <string_view>

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

  // Only a small regular file is ever read.
  SDL_PathInfo info{};
  if (!SDL_GetPathInfo(path.c_str(), &info)) {
    return scores;  // Not there yet: first run.
  }
  if (info.type != SDL_PATHTYPE_FILE || info.size > HighScores::kMaxTextBytes) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Ignoring unexpected high score file");
    return scores;
  }

  std::size_t size = 0;
  void* const data = SDL_LoadFile(path.c_str(), &size);
  if (data == nullptr) {
    return scores;
  }
  // The size is checked again: the file could have changed since the stat.
  const bool ok = size <= HighScores::kMaxTextBytes &&
                  scores.parse(std::string_view(static_cast<const char*>(data), size));
  SDL_free(data);
  if (!ok) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "High score file is not valid; starting fresh");
  }
  return scores;
}

bool saveHighScores(const HighScores& scores) {
  const std::string path = scoreFilePath();
  if (path.empty()) {
    return false;
  }
  const std::string text = scores.serialize();
  const std::string temporary = path + ".tmp";
  if (!SDL_SaveFile(temporary.c_str(), text.data(), text.size())) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not write high scores: %s", SDL_GetError());
    return false;
  }
  if (!SDL_RenamePath(temporary.c_str(), path.c_str())) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not save high scores: %s", SDL_GetError());
    SDL_RemovePath(temporary.c_str());
    return false;
  }
  return true;
}

}  // namespace pinball
