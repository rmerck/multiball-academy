#include "platform/ScoreStore.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
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

// Set when the score file exists but could not be read this run. Saving is
// then refused, so a table that may be perfectly good is not replaced.
bool saveBlocked = false;

// Removes temporary files that a save interrupted by a crash left behind:
// regular files named like the score file plus the six-character suffix.
void removeStaleTemporaries(const std::string& scorePath) {
  namespace fs = std::filesystem;
  std::error_code error;
  const fs::path score(scorePath);
  const std::string prefix = score.filename().string() + ".";
  constexpr std::size_t kSuffixLength = 6;
  for (fs::directory_iterator it(score.parent_path(), error), end; !error && it != end;
       it.increment(error)) {
    const std::string name = it->path().filename().string();
    if (name.size() == prefix.size() + kSuffixLength && name.compare(0, prefix.size(), prefix) == 0 &&
        it->symlink_status(error).type() == fs::file_type::regular) {
      fs::remove(it->path(), error);
    }
  }
}

}  // namespace

HighScores loadHighScores() {
  HighScores scores;
  saveBlocked = false;
  const std::string path = scoreFilePath();
  if (path.empty()) {
    return scores;
  }
  removeStaleTemporaries(path);

  std::vector<std::uint8_t> bytes;
  ReadResult result = readRegularFile(path, HighScores::kMaxTextBytes, bytes);
  if (result == ReadResult::Error) {
    // Often passing (too many open files, an interrupted call): try once more.
    result = readRegularFile(path, HighScores::kMaxTextBytes, bytes);
  }
  if (result == ReadResult::Missing) {
    return scores;  // Not there yet: first run.
  }
  if (result == ReadResult::Error) {
    // Still unreadable. It may be a good table, so neither move nor replace
    // it: play on with an empty table and save nothing this run.
    saveBlocked = true;
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Could not read the high score file; scores will not be saved this run");
    return scores;
  }
  const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  if (result != ReadResult::Ok || !scores.parse(text)) {
    // Not a plain file, too large, or not valid. Keep it for inspection
    // instead of letting the next save replace it: it may come from a newer
    // version. Renaming moves a link itself, never what it points at.
    const std::string rejected = path + ".bad";
    if (SDL_RenamePath(path.c_str(), rejected.c_str())) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "High score file is not usable; set aside as %s",
                  rejected.c_str());
    } else {
      saveBlocked = true;
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "High score file is not usable and could not be set aside (%s); scores will "
                  "not be saved this run",
                  SDL_GetError());
    }
    return HighScores{};
  }
  return scores;
}

bool saveHighScores(const HighScores& scores) {
  if (saveBlocked) {
    return false;
  }
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
