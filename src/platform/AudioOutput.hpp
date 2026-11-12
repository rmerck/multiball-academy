#pragma once

#include <SDL3/SDL.h>

#include <memory>

#include "core/Sound.hpp"

namespace pinball {

// Plays the mixer's output on the default audio device. Samples are pulled
// from the mixer on the main thread and queued, so the mixer needs no locking.
class AudioOutput {
 public:
  // Returns nullptr, after logging a warning, if no device can be opened.
  // The game then simply runs silent.
  [[nodiscard]] static std::unique_ptr<AudioOutput> open();

  ~AudioOutput();
  AudioOutput(const AudioOutput&) = delete;
  AudioOutput& operator=(const AudioOutput&) = delete;

  // Keeps the device supplied with sound. Call once per frame.
  void pump(SoundMixer& mixer);

 private:
  explicit AudioOutput(SDL_AudioStream* stream) : stream_(stream) {}

  SDL_AudioStream* stream_;
  // Frames to keep queued, worked out from the device's buffer size.
  int targetFrames_ = 0;
  int pumpsUntilDeviceCheck_ = 0;
};

}  // namespace pinball
