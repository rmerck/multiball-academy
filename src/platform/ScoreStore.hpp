#pragma once

#include <string>
#include <utility>

#include "core/HighScores.hpp"
#include "platform/SafeFile.hpp"

namespace pinball {

// Reads and writes the high score table in the per-user preferences
// directory. Nothing there is touched until load or save is called.
class ScoreStore {
 public:
  // directory: where the file is kept, ending in a separator; empty (the
  // default) for the per-user preferences directory. Only tests give one.
  // A separator is added if it is missing.
  explicit ScoreStore(std::string directory = {})
      : directory_(withTrailingSeparator(std::move(directory))) {}

  // The file is small plain text; whatever is on disk is validated in full
  // before any of it is used, and a bad or missing file just means an empty
  // table.
  [[nodiscard]] HighScores load();

  // Writes the table. Written to a temporary file and renamed into place, so
  // a crash mid-write cannot leave a half-written table. Returns false, after
  // logging, if it could not be saved; the game carries on regardless.
  bool save(const HighScores& scores) const;

 private:
  // Set when the score file exists but the last load could not read it, or
  // found it unusable and could not set it aside. Saving is then refused, so
  // a table that may be perfectly good is not replaced.
  bool saveBlocked_ = false;
  std::string directory_;
  [[nodiscard]] std::string filePath() const;
};

}  // namespace pinball
