#pragma once

#include "core/HighScores.hpp"

namespace pinball {

// Reads and writes the high score table in the per-user preferences
// directory. The file is small plain text; whatever is on disk is validated
// in full before any of it is used, and a bad or missing file just means an
// empty table.
HighScores loadHighScores();

// Writes the table. Written to a temporary file and renamed into place, so a
// crash mid-write cannot leave a half-written table. Returns false, after
// logging, if it could not be saved; the game carries on regardless.
bool saveHighScores(const HighScores& scores);

}  // namespace pinball
