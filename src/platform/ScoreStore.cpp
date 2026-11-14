#include "platform/ScoreStore.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/Version.hpp"
#include "platform/SafeFile.hpp"

namespace pinball {

namespace {

constexpr const char* kFileName = "highscores.txt";

// Full path of the score file in the preferences directory, or empty if
// there is none.
std::string prefsScoreFilePath() {
  // Created if missing; the returned string is ours to free.
  const std::unique_ptr<char, decltype([](char* text) { SDL_free(text); })> directory(
      SDL_GetPrefPath(kDataOrganisation, kDataDirectory));
  if (!directory) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No preferences directory: %s", SDL_GetError());
    return {};
  }
  return std::string(directory.get()) + kFileName;
}

}  // namespace

std::string ScoreStore::filePath() const {
  return directory_.empty() ? prefsScoreFilePath() : directory_ + kFileName;
}

HighScores ScoreStore::load() {
  HighScores scores;
  saveBlocked_ = false;
  const std::string path = filePath();
  if (path.empty()) {
    return scores;
  }
  // Left by a save that a crash interrupted.
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
    saveBlocked_ = true;
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Could not read the high score file; scores will not be saved this run");
    return scores;
  }
  const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  if (result != ReadResult::Ok || !scores.parse(text)) {
    // Not a plain file, too large, or not valid. Keep it for inspection
    // instead of letting the next save replace it: it may come from a newer
    // version. It goes to the first free name among .bad, .bad.1 ... .bad.9,
    // so a file set aside earlier is never replaced by this one. The move
    // takes a link itself, never what it points at.
    const std::string rejected = setAside(path);
    if (!rejected.empty()) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "High score file is not usable; set aside as %s",
                  rejected.c_str());
    } else {
      // Every name is taken, or the move failed. The file stays where it is
      // and must not be overwritten.
      saveBlocked_ = true;
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "High score file is not usable and could not be set aside; scores will not "
                  "be saved this run");
    }
    return HighScores{};
  }
  return scores;
}

bool ScoreStore::save(const HighScores& scores) const {
  if (saveBlocked_) {
    return false;
  }
  const std::string path = filePath();
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
