#pragma once

#include "core/HighScores.hpp"

namespace pinball {

// Reads and writes the high score table in the per-user preferences
// directory. Nothing there is touched until load or save is called.
class ScoreStore {
 public:
  // The file is small plain text; whatever is on disk is validated in full
  // before any of it is used, and a bad or missing file just means an empty
  // table.
  [[nodiscard]] HighScores load();

  // Writes the table. Written to a temporary file and renamed into place, so
  // a crash mid-write cannot leave a half-written table. Returns false, after
  // logging, if it could not be saved; the game carries on regardless.
  bool save(const HighScores& scores) const;

 private:
  // Set when the score file exists but the last load could not read it.
  // Saving is then refused, so a table that may be perfectly good is not
  // replaced.
  bool saveBlocked_ = false;
};

}  // namespace pinball
