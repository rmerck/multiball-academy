#pragma once

#include "core/Settings.hpp"

namespace pinball {

// Reads and writes the settings file beside the high score file, in the
// per-user preferences directory, the same safe way (platform/SafeFile.hpp):
// a bounded read of a regular file only, validated in full before any of it
// is used, and an all-or-nothing replace on save.
class SettingsStore {
 public:
  // A missing or bad file means the defaults. A bad one is set aside (as
  // .bad, .bad.1 ...) so that it can be looked at, and replaced on the next
  // save; one that cannot be read or set aside blocks saving for the run.
  [[nodiscard]] Settings load();
  // Returns false, after logging, if it could not be saved.
  bool save(const Settings& settings) const;

 private:
  bool saveBlocked_ = false;
};

}  // namespace pinball
