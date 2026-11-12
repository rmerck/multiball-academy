#include "platform/AudioOutput.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace pinball {

namespace {

// Keep about this much sound queued: two 60 Hz frames plus a margin. Enough
// to ride out a slow frame, short enough that a flipper click follows the
// key press without a noticeable delay.
constexpr int kQueueMilliseconds = 40;
constexpr int kTargetQueuedFrames = SoundMixer::kSampleRate * kQueueMilliseconds / 1000;
constexpr std::size_t kMaxFramesPerPump = 4096;

}  // namespace

std::unique_ptr<AudioOutput> AudioOutput::open() {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No audio: %s", SDL_GetError());
    return nullptr;
  }
  SDL_AudioSpec spec{};
  spec.format = SDL_AUDIO_F32;
  spec.channels = 1;
  spec.freq = SoundMixer::kSampleRate;
  SDL_AudioStream* stream =
      SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (stream == nullptr) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No audio device: %s", SDL_GetError());
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return nullptr;
  }
  // The device starts paused.
  if (!SDL_ResumeAudioStreamDevice(stream)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Audio would not start: %s", SDL_GetError());
    SDL_DestroyAudioStream(stream);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return nullptr;
  }
  return std::unique_ptr<AudioOutput>(new AudioOutput(stream));
}

AudioOutput::~AudioOutput() {
  SDL_DestroyAudioStream(stream_);
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void AudioOutput::pump(SoundMixer& mixer) {
  const int queuedBytes = SDL_GetAudioStreamQueued(stream_);
  if (queuedBytes < 0) {
    return;
  }
  const int queuedFrames = queuedBytes / static_cast<int>(sizeof(float));
  if (queuedFrames >= kTargetQueuedFrames) {
    return;
  }
  const std::size_t frames = std::min<std::size_t>(
      static_cast<std::size_t>(kTargetQueuedFrames - queuedFrames), kMaxFramesPerPump);
  std::array<float, kMaxFramesPerPump> buffer{};
  mixer.render(buffer.data(), frames);
  // If this fails the sound for one frame is lost; nothing else depends on it.
  SDL_PutAudioStreamData(stream_, buffer.data(), static_cast<int>(frames * sizeof(float)));
}

}  // namespace pinball
