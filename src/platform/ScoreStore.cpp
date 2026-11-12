#include "platform/ScoreStore.hpp"

#include <SDL3/SDL.h>

#include <array>
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

// Reads at most capacity bytes. Returns the count, or capacity + 1 if the
// file holds more, so the caller never buffers an oversized file.
std::size_t readBounded(SDL_IOStream* stream, char* out, std::size_t capacity) {
  std::size_t total = 0;
  while (total < capacity) {
    const std::size_t got = SDL_ReadIO(stream, out + total, capacity - total);
    if (got == 0) {
      return total;
    }
    total += got;
  }
  char extra = 0;
  return SDL_ReadIO(stream, &extra, 1) == 0 ? total : capacity + 1;
}

// Writes the whole text and reports failure from any stage, including the
// flush and close where a full disk shows up.
bool writeAll(const char* path, std::string_view text) {
  SDL_IOStream* const stream = SDL_IOFromFile(path, "wb");
  if (stream == nullptr) {
    return false;
  }
  bool ok = SDL_WriteIO(stream, text.data(), text.size()) == text.size();
  ok = SDL_FlushIO(stream) && ok;
  ok = SDL_CloseIO(stream) && ok;
  return ok;
}

}  // namespace

HighScores loadHighScores() {
  HighScores scores;
  const std::string path = scoreFilePath();
  if (path.empty()) {
    return scores;
  }
  SDL_PathInfo info{};
  if (!SDL_GetPathInfo(path.c_str(), &info)) {
    return scores;  // Not there yet: first run.
  }

  // The read itself is bounded, so a file that is large, or grows while it is
  // being read, costs nothing.
  std::array<char, HighScores::kMaxTextBytes> buffer{};
  std::size_t size = buffer.size() + 1;
  if (info.type == SDL_PATHTYPE_FILE) {
    if (SDL_IOStream* const stream = SDL_IOFromFile(path.c_str(), "rb")) {
      size = readBounded(stream, buffer.data(), buffer.size());
      SDL_CloseIO(stream);
    }
  }
  if (size > buffer.size() || !scores.parse(std::string_view(buffer.data(), size))) {
    // Keep the unreadable file for inspection instead of overwriting it at
    // the next save. It may come from a newer version.
    const std::string rejected = path + ".bad";
    SDL_RemovePath(rejected.c_str());
    SDL_RenamePath(path.c_str(), rejected.c_str());
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "High score file is not valid; set aside as %s", rejected.c_str());
    return HighScores{};
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
  // Clear anything left at the temporary name, a link included, so the write
  // creates a fresh file. The real file is replaced only by a complete one.
  SDL_RemovePath(temporary.c_str());
  if (!writeAll(temporary.c_str(), text)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not write high scores: %s", SDL_GetError());
    SDL_RemovePath(temporary.c_str());
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
