#include "platform/SaveStore.hpp"

#include <SDL3/SDL.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/Snapshot.hpp"
#include "core/Version.hpp"
#include "platform/SafeFile.hpp"

namespace pinball {

namespace {

constexpr const char* kFileName = "savedgame.bin";

}  // namespace

std::string SaveStore::path() const {
  if (!directory_.empty()) {
    return directory_ + kFileName;
  }
  const std::unique_ptr<char, decltype([](char* text) { SDL_free(text); })> directory(
      SDL_GetPrefPath(kDataOrganisation, kDataDirectory));
  if (!directory) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No preferences directory: %s", SDL_GetError());
    return {};
  }
  return std::string(directory.get()) + kFileName;
}

std::optional<save::Snapshot> SaveStore::load() {
  const std::string file = path();
  if (file.empty()) {
    return std::nullopt;
  }
  // Left by a write that a crash interrupted.
  removeStaleTemporaries(file);

  std::vector<std::uint8_t> bytes;
  ReadResult result = readRegularFile(file, save::kMaxFileBytes, bytes);
  if (result == ReadResult::Error) {
    result = readRegularFile(file, save::kMaxFileBytes, bytes);
  }
  if (result == ReadResult::Missing) {
    return std::nullopt;
  }
  if (result == ReadResult::Error) {
    // Unreadable for now: left alone, and not offered.
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not read the saved game");
    return std::nullopt;
  }
  std::optional<save::Snapshot> snapshot;
  if (result == ReadResult::Ok) {
    snapshot = save::decode(std::span<const std::uint8_t>(bytes));
  }
  if (!snapshot) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "The saved game cannot be resumed by this version; deleted");
    remove();
  }
  return snapshot;
}

bool SaveStore::write(const save::Snapshot& snapshot) {
  const std::string file = path();
  if (file.empty()) {
    return false;
  }
  const std::string bytes = save::encode(snapshot);
  if (bytes.empty() || !replaceFile(file, bytes)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not save the game in progress");
    return false;
  }
  return true;
}

bool SaveStore::remove() {
  const std::string file = path();
  if (file.empty()) {
    return true;
  }
  // std::remove unlinks: a link is removed itself, never what it points at.
  errno = 0;
  if (std::remove(file.c_str()) != 0 && errno != ENOENT) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not delete the saved game (error %d)",
                errno);
    return false;
  }
  return true;
}

}  // namespace pinball
