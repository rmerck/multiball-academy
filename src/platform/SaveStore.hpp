#pragma once

#include <optional>
#include <string>
#include <utility>

#include "core/Snapshot.hpp"

namespace pinball {

// The saved game: one small binary file beside the high scores, in the
// per-user preferences directory (docs/RESUME.md). Nothing there is touched
// until a method is called.
class SaveStore {
 public:
  // directory: where the file is kept, ending in a separator; empty (the
  // default) for the per-user preferences directory. Only tests give one.
  explicit SaveStore(std::string directory = {}) : directory_(std::move(directory)) {}

  // Reads the file and checks every byte of it (save::decode). A file that
  // is there and is not a game this build can resume (damaged, cut short,
  // too large, from another version or table, not a plain file) is deleted:
  // there is nothing in it worth keeping. Nothing there: nullopt.
  [[nodiscard]] std::optional<save::Snapshot> load();

  // Writes the game. To a temporary file that is flushed and renamed into
  // place, so a crash mid-write leaves the old file or the new, never half
  // of one. Returns false, after logging, if it could not be saved.
  bool write(const save::Snapshot& snapshot);

  // Deletes the file. True once there is no file (also when there was none).
  bool remove();

 private:
  // The file's full path, or empty with no preferences directory.
  [[nodiscard]] std::string path() const;
  std::string directory_;
};

}  // namespace pinball
