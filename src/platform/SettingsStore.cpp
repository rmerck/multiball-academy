#include "platform/SettingsStore.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/Version.hpp"
#include "platform/SafeFile.hpp"

namespace pinball {

namespace {

constexpr const char* kFileName = "settings.txt";

// Full path of the settings file, or empty if there is no preferences
// directory.
std::string settingsFilePath() {
  const std::unique_ptr<char, decltype([](char* text) { SDL_free(text); })> directory(
      SDL_GetPrefPath(kDataOrganisation, kDataDirectory));
  if (!directory) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No preferences directory: %s", SDL_GetError());
    return {};
  }
  return std::string(directory.get()) + kFileName;
}

}  // namespace

std::string SettingsStore::filePath() const {
  return directory_.empty() ? settingsFilePath() : directory_ + kFileName;
}

Settings SettingsStore::load() {
  Settings settings;
  saveBlocked_ = false;
  const std::string path = filePath();
  if (path.empty()) {
    return settings;
  }
  removeStaleTemporaries(path);
  std::vector<std::uint8_t> bytes;
  ReadResult result = readRegularFile(path, Settings::kMaxTextBytes, bytes);
  if (result == ReadResult::Error) {
    result = readRegularFile(path, Settings::kMaxTextBytes, bytes);
  }
  if (result == ReadResult::Missing) {
    return settings;
  }
  if (result == ReadResult::Error) {
    saveBlocked_ = true;
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Could not read the settings file; settings will not be saved this run");
    return settings;
  }
  const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  if (result != ReadResult::Ok || !settings.parse(text)) {
    const std::string rejected = setAside(path);
    if (!rejected.empty()) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Settings file is not usable; set aside as %s",
                  rejected.c_str());
    } else {
      saveBlocked_ = true;
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "Settings file is not usable and could not be set aside; settings will not "
                  "be saved this run");
    }
    return Settings{};
  }
  return settings;
}

bool SettingsStore::save(const Settings& settings) const {
  if (saveBlocked_) {
    return false;
  }
  const std::string path = filePath();
  if (path.empty()) {
    return false;
  }
  if (!replaceFile(path, settings.serialize())) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not save the settings");
    return false;
  }
  return true;
}

}  // namespace pinball
