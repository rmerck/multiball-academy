#pragma once

#include <cstdint>

namespace pinball {

// The background tune: an original eight-bar loop for bass, arpeggio and
// lead, synthesised a sample at a time like the sound effects. Pure
// arithmetic with no device access and no stored audio.
class Music {
 public:
  static constexpr int kSampleRate = 48000;
  // One sixteenth note. 6400 samples is 112.5 beats per minute.
  static constexpr std::uint32_t kStepSamples = 6400;
  static constexpr std::uint32_t kStepsPerBar = 16;
  static constexpr std::uint32_t kBars = 8;
  static constexpr std::uint32_t kLoopSamples = kStepSamples * kStepsPerBar * kBars;
  // Loudest sample next() can return.
  static constexpr float kPeak = 0.32F;

  // Starts or stops the tune. It fades over 50 ms instead of cutting, and
  // picks up where it stopped.
  void setPlaying(bool playing) noexcept { playing_ = playing; }
  bool playing() const noexcept { return playing_; }
  // True while any sound is still coming out, including a fade-out.
  bool audible() const noexcept { return playing_ || gain_ > 0.0F; }

  // The next sample, within [-kPeak, kPeak]. Silent, and standing still,
  // while stopped.
  float next() noexcept;

 private:
  struct Tone {
    float phase = 0.0F;      // Cycles, 0 to 1.
    float increment = 0.0F;  // Cycles per sample.
    float level = 0.0F;      // Envelope, 0 to 1.
    float decay = 1.0F;      // Envelope multiplier per sample.
    bool attacking = false;
  };

  static void strike(Tone& tone, int midiNote, float decay) noexcept;
  static float advance(Tone& tone) noexcept;
  void startStep() noexcept;

  Tone bass_;
  Tone arpeggio_;
  Tone lead_;
  std::uint32_t step_ = 0;          // 0 to kStepsPerBar * kBars - 1.
  std::uint32_t sampleInStep_ = 0;  // 0 to kStepSamples - 1.
  float gain_ = 0.0F;               // Fade, 0 to 1.
  bool playing_ = false;
};

}  // namespace pinball
