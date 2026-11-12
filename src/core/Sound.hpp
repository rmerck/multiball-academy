#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace pinball {

// Every sound the game can make.
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
static_assert(static_cast<std::size_t>(Sound::Nudge) + 1 == kSoundCount,
              "kSoundCount must follow the last Sound enumerator");
static_assert(kSoundCount <= 32, "SoundSet holds one bit per sound");

// A set of sounds, one bit each. A sound plays at most once per step.
using SoundSet = std::uint32_t;
constexpr SoundSet soundBit(Sound sound) noexcept {
  return SoundSet{1} << static_cast<unsigned>(sound);
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
  // least left to play is replaced.
  void trigger(SoundSet sounds) noexcept;

  // Writes frames mono samples in [-1, 1] to out. out may be null only when
  // frames is 0.
  void render(float* out, std::size_t frames) noexcept;

  // Number of voices currently sounding.
  std::size_t activeVoices() const noexcept;

  void setMuted(bool muted) noexcept { muted_ = muted; }
  bool muted() const noexcept { return muted_; }

 private:
  enum class Wave : std::uint8_t { Sine, Square, Triangle, Noise };
  struct Voice {
    bool active = false;
    Wave wave = Wave::Sine;
    float startHz = 0.0F;
    float endHz = 0.0F;
    float gain = 0.0F;
    std::uint32_t delay = 0;     // Samples to wait before starting.
    std::uint32_t length = 0;    // Samples of sound.
    std::uint32_t position = 0;  // Samples played so far.
    float phase = 0.0F;          // Cycles, 0 to 1.
  };

  void start(Wave wave, float startHz, float endHz, float seconds, float gain,
             float delaySeconds = 0.0F) noexcept;
  void startSound(Sound sound) noexcept;

  std::array<Voice, kVoices> voices_{};
  // Samples each sound must still wait before it may start again.
  std::array<std::uint32_t, kSoundCount> holdoff_{};
  std::uint32_t noiseState_ = 0x12345678U;
  bool muted_ = false;
};

}  // namespace pinball
