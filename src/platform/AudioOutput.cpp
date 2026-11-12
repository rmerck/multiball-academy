#include "platform/AudioOutput.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace pinball {

namespace {

// Keep at least this much sound queued: two 60 Hz frames plus a margin.
// Enough to ride out a slow frame, short enough that a flipper click follows
// the key press without a noticeable delay.
constexpr int kQueueMilliseconds = 40;
constexpr int kMinQueuedFrames = SoundMixer::kSampleRate * kQueueMilliseconds / 1000;
constexpr int kMaxFramesPerPump = 4096;
// Slack on top of two device buffers, for a device that takes its buffer a
// little early: 5 ms.
constexpr int kMarginFrames = SoundMixer::kSampleRate / 200;
// The device is asked again for its buffer size this often, in pumps, in
// case the default device has changed.
constexpr int kPumpsPerDeviceCheck = 120;

// How many frames to keep queued for a device that takes deviceFrames at a
// time at deviceHz. The device empties the queue a whole buffer at a time,
// and takes two between pumps whenever one frame is late, so less than two
// buffers queued leaves a gap of silence in the second.
int targetFor(int deviceFrames, int deviceHz) noexcept {
  if (deviceFrames <= 0 || deviceHz <= 0) {
    return kMinQueuedFrames;
  }
  const std::int64_t ours = (static_cast<std::int64_t>(deviceFrames) * SoundMixer::kSampleRate +
                             deviceHz - 1) /
                            deviceHz;
  const std::int64_t wanted = 2 * ours + kMarginFrames;
  return static_cast<int>(
      std::clamp<std::int64_t>(wanted, kMinQueuedFrames, kMaxFramesPerPump));
}

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
  if (pumpsUntilDeviceCheck_-- <= 0) {
    pumpsUntilDeviceCheck_ = kPumpsPerDeviceCheck;
    SDL_AudioSpec device{};
    int deviceFrames = 0;
    const bool known =
        SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream_), &device, &deviceFrames);
    targetFrames_ = known ? targetFor(deviceFrames, device.freq) : kMinQueuedFrames;
  }
  // Bytes in the format the stream is fed in: mono float, so four a frame.
  const int queuedBytes = SDL_GetAudioStreamQueued(stream_);
  if (queuedBytes < 0) {
    return;
  }
  const int queuedFrames = queuedBytes / static_cast<int>(sizeof(float));
  if (queuedFrames >= targetFrames_) {
    return;
  }
  // The queue is only ever topped up to the target, so a hitch costs a gap
  // of silence and never a delay that grows.
  const auto frames = static_cast<std::size_t>(targetFrames_ - queuedFrames);
  std::array<float, kMaxFramesPerPump> buffer{};
  mixer.render(buffer.data(), frames);
  // If this fails the sound for one frame is lost; nothing else depends on it.
  SDL_PutAudioStreamData(stream_, buffer.data(), static_cast<int>(frames * sizeof(float)));
}

}  // namespace pinball
