#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "core/Music.hpp"

namespace pinball {

// Every sound the game can make. The provisional rules cue none of
// ExtraBall, BankComplete, MissionSelect, MissionAccept, MissionComplete and
// Promotion. They stay until the sound list of the table's own rules
// replaces this one: the mixer's voice tests (tests/SoundTest.cpp) are
// tuned to the whole set, so taking some out is not a change to make alone.
enum class Sound : std::uint8_t {
  Flipper,
  Launch,
  Bumper,
  Slingshot,
  Target,
  Rollover,
  HoleIn,
  HoleOut,
  Drain,
  BallSaved,
  ExtraBall,
  BankComplete,
  MissionSelect,
  MissionAccept,
  MissionComplete,
  Promotion,
  TiltWarning,
  Tilt,
  GameOver,
  Nudge,
};
inline constexpr std::size_t kSoundCount = 20;
static_assert(std::to_underlying(Sound::Nudge) + 1 == kSoundCount,
              "kSoundCount must follow the last Sound enumerator");
static_assert(kSoundCount <= 32, "SoundSet holds one bit per sound");

// A set of sounds, one bit each. A sound plays at most once per step.
using SoundSet = std::uint32_t;
constexpr SoundSet soundBit(Sound sound) noexcept {
  return SoundSet{1} << std::to_underlying(sound);
}

// Synthesises every sound from oscillators and noise, so the game ships no
// audio files. Pure arithmetic with no device access: the platform layer
// asks it for samples and plays them.
class SoundMixer {
 public:
  static constexpr int kSampleRate = 48000;
  static constexpr std::size_t kVoices = 24;
  // Shortest gap between two starts of the same sound: 30 ms.
  static constexpr std::uint32_t kRetriggerSamples = 1440;

  // Starts every sound in the set. A sound that started within the last
  // kRetriggerSamples is skipped, so one event repeated in quick succession
  // cannot stack copies of itself. If all voices are busy the voice with the
  // least left to play is replaced, never one of the same sound's own, and
  // what it was playing dies away over a few milliseconds instead of cutting.
  void trigger(SoundSet sounds) noexcept;

  // Fills out with mono samples in [-1, 1].
  void render(std::span<float> out) noexcept;
  // The same for a pointer and a count. A null out does nothing.
  void render(float* out, std::size_t frames) noexcept {
    if (out != nullptr) {
      render(std::span<float>(out, frames));
    }
  }

  // Number of voices currently sounding.
  [[nodiscard]] std::size_t activeVoices() const noexcept;

  // The background tune, mixed under the effects. Off until asked for; it
  // fades in and out, and resumes where it stopped.
  void setMusicPlaying(bool playing) noexcept { music_.setPlaying(playing); }
  [[nodiscard]] bool musicPlaying() const noexcept { return music_.playing(); }
  // True while anything at all is sounding: a voice, or the tune.
  [[nodiscard]] bool busy() const noexcept { return music_.audible() || activeVoices() > 0; }

  void setMuted(bool muted) noexcept { muted_ = muted; }
  [[nodiscard]] bool muted() const noexcept { return muted_; }

 private:
  // Noise is white. SweptNoise is noise with a pitch: random levels joined by
  // straight lines, a new level every cycle, so it darkens or brightens as
  // its pitch glides.
  enum class Wave : std::uint8_t { Sine, Square, Triangle, Noise, SweptNoise };
  struct Voice {
    bool active = false;
    Wave wave = Wave::Sine;
    // Pitch in cycles per sample: startStep at the first sample, gliding by
    // stepGlide in all over the length. White noise has no pitch.
    float startStep = 0.0F;
    float stepGlide = 0.0F;
    float gain = 0.0F;
    std::uint32_t delay = 0;     // Samples to wait before starting.
    std::uint32_t length = 0;    // Samples of sound.
    std::uint32_t position = 0;  // Samples played so far.
    float perLength = 0.0F;      // 1 / length.
    float phase = 0.0F;          // Cycles, 0 to 1.
    float last = 0.0F;           // The latest sample this voice added to the mix.
    float held = 0.0F;           // SweptNoise: the level this cycle leaves,
    float target = 0.0F;         // and the level it reaches.
    std::uint32_t batch = 0;     // Which startSound call began it.
  };

  void start(Wave wave, float startHz, float endHz, float seconds, float gain,
             float delaySeconds = 0.0F) noexcept;
  void startSound(Sound sound) noexcept;
  // White noise has no pitch, so it takes none.
  void startNoise(float seconds, float gain) noexcept;
  // Noise whose brightness glides: a new random level startHz times a
  // second at first, endHz at the end.
  void startSweptNoise(float startHz, float endHz, float seconds, float gain) noexcept;

  std::array<Voice, kVoices> voices_{};
  Music music_;
  // Samples each sound must still wait before it may start again.
  std::array<std::uint32_t, kSoundCount> holdoff_{};
  std::uint32_t noiseState_ = 0x12345678U;
  std::uint32_t batch_ = 0;
  // What replaced voices were playing when they were cut off, dying away.
  float cutTail_ = 0.0F;
  bool muted_ = false;
  float muteGain_ = 1.0F;  // Follows muted_, 0 to 1.
};

}  // namespace pinball
