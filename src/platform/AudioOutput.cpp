#include "platform/AudioOutput.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

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
constexpr int targetFor(int deviceFrames, int deviceHz) noexcept {
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
// A small buffer keeps the 40 ms floor; the usual 1024 frames gives two
// buffers and the margin; other rates are converted, rounding up; nothing
// the device reports can take the target below the floor or past the buffer
// pump() renders into.
static_assert(targetFor(256, 48000) == kMinQueuedFrames);
static_assert(targetFor(1024, 48000) == 2 * 1024 + kMarginFrames);
static_assert(targetFor(1024, 44100) == 2 * 1115 + kMarginFrames);
static_assert(targetFor(2048, 96000) == 2 * 1024 + kMarginFrames);
static_assert(targetFor(8192, 48000) == kMaxFramesPerPump);
static_assert(targetFor(std::numeric_limits<int>::max(), 1) == kMaxFramesPerPump);
static_assert(targetFor(0, 48000) == kMinQueuedFrames);
static_assert(targetFor(-1024, 48000) == kMinQueuedFrames);
static_assert(targetFor(1024, 0) == kMinQueuedFrames);
static_assert(targetFor(1024, -48000) == kMinQueuedFrames);
static_assert(kMinQueuedFrames > 0 && kMinQueuedFrames <= kMaxFramesPerPump);

}  // namespace

namespace {

// Opens and starts the default playback device. The audio subsystem must be
// up. Returns null, after logging a warning, if there is no usable device.
SDL_AudioStream* openStream() {
  const SDL_AudioSpec spec{.format = SDL_AUDIO_F32, .channels = 1, .freq = SoundMixer::kSampleRate};
  SDL_AudioStream* stream =
      SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (stream == nullptr) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No audio device: %s", SDL_GetError());
    return nullptr;
  }
  // The device starts paused.
  if (!SDL_ResumeAudioStreamDevice(stream)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Audio would not start: %s", SDL_GetError());
    SDL_DestroyAudioStream(stream);
    return nullptr;
  }
  return stream;
}

}  // namespace

std::unique_ptr<AudioOutput> AudioOutput::open() {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No audio: %s", SDL_GetError());
    return nullptr;
  }
  SDL_AudioStream* stream = openStream();
  if (stream == nullptr) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return nullptr;
  }
  return std::unique_ptr<AudioOutput>(new AudioOutput(stream));
}

AudioOutput::~AudioOutput() {
  SDL_DestroyAudioStream(stream_);
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void AudioOutput::suspend() {
  suspended_ = true;
  SDL_PauseAudioStreamDevice(stream_);
  SDL_ClearAudioStream(stream_);
}

void AudioOutput::resume() {
  suspended_ = false;
  SDL_ResumeAudioStreamDevice(stream_);
  // The default device may be another one by now.
  pumpsUntilDeviceCheck_ = 0;
}

void AudioOutput::pump(SoundMixer& mixer) {
  if (suspended_) {
    return;
  }
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
  mixer.render(std::span<float>(buffer).first(frames));
  // If this fails the sound for one frame is lost; nothing else depends on it.
  SDL_PutAudioStreamData(stream_, buffer.data(), static_cast<int>(frames * sizeof(float)));
}

AudioOpener::~AudioOpener() {
  if (thread_ != nullptr) {
    // Waits as long as the open takes, which where it is stuck is about
    // half a minute. The alternative, leaving the worker to finish by
    // itself, would have it use SDL's audio after SDL has shut down.
    SDL_WaitThread(thread_, nullptr);
    thread_ = nullptr;
    if (stream_ != nullptr) {
      SDL_DestroyAudioStream(stream_);
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }
}

int SDLCALL AudioOpener::work(void* self) {
  auto* const opener = static_cast<AudioOpener*>(self);
  opener->stream_ = openStream();
  SDL_SetAtomicInt(&opener->state_, kDone);
  return 0;
}

void AudioOpener::start() {
  if (thread_ != nullptr) {
    return;
  }
  // Up here and not on the worker: bringing a subsystem up is not something
  // SDL promises to do safely on two threads at once. It opens no device.
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No audio: %s", SDL_GetError());
    return;
  }
  stream_ = nullptr;
  SDL_SetAtomicInt(&state_, kOpening);
  thread_ = SDL_CreateThread(&AudioOpener::work, "audio open", this);
  if (thread_ == nullptr) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No audio: %s", SDL_GetError());
    SDL_SetAtomicInt(&state_, kIdle);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }
}

bool AudioOpener::busy() const noexcept {
  return SDL_GetAtomicInt(&state_) == kOpening;
}

std::unique_ptr<AudioOutput> AudioOpener::take() {
  if (thread_ == nullptr || SDL_GetAtomicInt(&state_) != kDone) {
    return nullptr;
  }
  // The worker has finished its work; this returns at once.
  SDL_WaitThread(thread_, nullptr);
  thread_ = nullptr;
  SDL_SetAtomicInt(&state_, kIdle);
  SDL_AudioStream* const stream = std::exchange(stream_, nullptr);
  if (stream == nullptr) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return nullptr;
  }
  // The AudioOutput takes over the subsystem reference start() took.
  return std::unique_ptr<AudioOutput>(new AudioOutput(stream));
}

}  // namespace pinball
