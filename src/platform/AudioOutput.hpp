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

  // Stops the device and drops what is queued, for when no frames will run
  // to keep it supplied (the application is in the background). resume
  // starts it again; pump then refills the queue. While suspended, pump
  // queues nothing and takes nothing from the mixer.
  void suspend();
  void resume();

 private:
  friend class AudioOpener;
  explicit AudioOutput(SDL_AudioStream* stream) : stream_(stream) {}

  SDL_AudioStream* stream_;
  bool suspended_ = false;
  // Frames to keep queued, worked out from the device's buffer size.
  int targetFrames_ = 0;
  int pumpsUntilDeviceCheck_ = 0;
};

// Opens the audio device on a thread of its own, for systems where that can
// take long: on iOS the open waits for the audio session and for the
// device's first buffers, on the calling thread, and a main thread that
// waits for seconds at launch gets the application killed. The game runs
// silent until the device is there, and for good if it never comes.
//
// Everything here except the open itself is to be called from one thread.
class AudioOpener {
 public:
  AudioOpener() = default;
  // Waits for an open still in flight: the worker uses SDL's audio, which
  // must not be shut down under it. A device nobody took is closed.
  ~AudioOpener();
  AudioOpener(const AudioOpener&) = delete;
  AudioOpener& operator=(const AudioOpener&) = delete;

  // Starts opening the default device. Does nothing while an earlier open
  // is still in flight or its device has not been taken.
  void start();
  // An open is in flight.
  [[nodiscard]] bool busy() const noexcept;
  // The device, once, when an open has finished and worked. Null while one
  // is in flight, when none was started, and when it failed (which the
  // worker has logged). Never waits.
  [[nodiscard]] std::unique_ptr<AudioOutput> take();

 private:
  enum : int { kIdle, kOpening, kDone };
  static int SDLCALL work(void* self);

  SDL_Thread* thread_ = nullptr;
  // Written by the worker before it sets state_ to kDone, read by the owner
  // after it has seen kDone.
  SDL_AudioStream* stream_ = nullptr;
  // mutable: SDL's getter takes a pointer to non-const.
  mutable SDL_AtomicInt state_{kIdle};
};

}  // namespace pinball
