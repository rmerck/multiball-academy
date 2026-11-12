#include "core/Sound.hpp"

#include <algorithm>
#include <cmath>

namespace pinball {

namespace {

constexpr float kTwoPi = 6.28318530717958647692F;
constexpr float kMasterGain = 0.55F;
// Fade in and out, in samples, so no voice starts or stops with a click.
constexpr std::uint32_t kFadeIn = 96;
constexpr std::uint32_t kFadeOut = 480;
// Mute and unmute ramp over 5 ms.
constexpr float kMuteStep = 1.0F / 240.0F;

// Musical notes, in Hz.
constexpr float kC5 = 523.25F;
constexpr float kE5 = 659.25F;
constexpr float kG5 = 783.99F;
constexpr float kC6 = 1046.5F;
constexpr float kE6 = 1318.5F;
constexpr float kG6 = 1568.0F;

}  // namespace

void SoundMixer::start(Wave wave, float startHz, float endHz, float seconds, float gain,
                       float delaySeconds) noexcept {
  // Reuse a free voice, or failing that the one with the least left to play,
  // counting any wait before it starts.
  Voice* chosen = nullptr;
  std::uint32_t leastLeft = 0;
  for (Voice& v : voices_) {
    if (!v.active) {
      chosen = &v;
      break;
    }
    const std::uint32_t left = v.delay + (v.length - std::min(v.position, v.length));
    if (chosen == nullptr || left < leastLeft) {
      chosen = &v;
      leastLeft = left;
    }
  }
  const float rate = static_cast<float>(kSampleRate);
  Voice& v = *chosen;
  v.active = true;
  v.wave = wave;
  v.startHz = std::clamp(startHz, 20.0F, 12000.0F);
  v.endHz = std::clamp(endHz, 20.0F, 12000.0F);
  v.gain = std::clamp(gain, 0.0F, 1.0F);
  v.delay = static_cast<std::uint32_t>(std::clamp(delaySeconds, 0.0F, 5.0F) * rate);
  v.length = std::max<std::uint32_t>(
      static_cast<std::uint32_t>(std::clamp(seconds, 0.005F, 5.0F) * rate), kFadeIn + kFadeOut);
  v.position = 0;
  v.phase = 0.0F;
}

void SoundMixer::startSound(Sound sound) noexcept {
  switch (sound) {
    case Sound::Flipper:
      start(Wave::Triangle, 150.0F, 70.0F, 0.06F, 0.55F);
      start(Wave::Noise, 2000.0F, 2000.0F, 0.02F, 0.20F);
      break;
    case Sound::Launch:
      start(Wave::Noise, 900.0F, 4000.0F, 0.28F, 0.30F);
      start(Wave::Sine, 110.0F, 330.0F, 0.25F, 0.35F);
      break;
    case Sound::Bumper:
      start(Wave::Sine, 620.0F, 210.0F, 0.11F, 0.60F);
      start(Wave::Noise, 3000.0F, 3000.0F, 0.025F, 0.25F);
      break;
    case Sound::Slingshot:
      start(Wave::Square, 330.0F, 160.0F, 0.07F, 0.30F);
      start(Wave::Noise, 2500.0F, 2500.0F, 0.04F, 0.30F);
      break;
    case Sound::Target:
      start(Wave::Sine, 1245.0F, 1245.0F, 0.22F, 0.40F);
      start(Wave::Sine, 1868.0F, 1868.0F, 0.12F, 0.18F);
      break;
    case Sound::Rollover:
      start(Wave::Sine, 880.0F, 1320.0F, 0.07F, 0.35F);
      break;
    case Sound::HoleIn:
      start(Wave::Sine, 900.0F, 110.0F, 0.42F, 0.45F);
      break;
    case Sound::HoleOut:
      start(Wave::Sine, 140.0F, 950.0F, 0.20F, 0.40F);
      start(Wave::Noise, 1500.0F, 1500.0F, 0.05F, 0.20F);
      break;
    case Sound::Drain:
      start(Wave::Triangle, kG5, kG5, 0.18F, 0.40F);
      start(Wave::Triangle, kE5, kE5, 0.18F, 0.40F, 0.16F);
      start(Wave::Triangle, kC5 * 0.5F, kC5 * 0.5F, 0.40F, 0.45F, 0.32F);
      break;
    case Sound::BallSaved:
      start(Wave::Sine, kC5, kC5, 0.10F, 0.35F);
      start(Wave::Sine, kG5, kG5, 0.16F, 0.35F, 0.10F);
      break;
    case Sound::ExtraBall:
      start(Wave::Sine, kC5, kC5, 0.10F, 0.40F);
      start(Wave::Sine, kE5, kE5, 0.10F, 0.40F, 0.09F);
      start(Wave::Sine, kG5, kG5, 0.10F, 0.40F, 0.18F);
      start(Wave::Sine, kC6, kC6, 0.30F, 0.45F, 0.27F);
      break;
    case Sound::BankComplete:
      start(Wave::Sine, kE5, kE5, 0.09F, 0.38F);
      start(Wave::Sine, kG5, kG5, 0.09F, 0.38F, 0.08F);
      start(Wave::Sine, kE6, kE6, 0.22F, 0.38F, 0.16F);
      break;
    case Sound::MissionSelect:
      start(Wave::Square, kC5, kC5, 0.08F, 0.20F);
      start(Wave::Square, kG5, kG5, 0.12F, 0.20F, 0.09F);
      break;
    case Sound::MissionAccept:
      start(Wave::Triangle, kC5, kC5, 0.10F, 0.40F);
      start(Wave::Triangle, kG5, kG5, 0.10F, 0.40F, 0.10F);
      start(Wave::Triangle, kC6, kC6, 0.25F, 0.40F, 0.20F);
      break;
    case Sound::MissionComplete:
      start(Wave::Sine, kC5, kC5, 0.12F, 0.40F);
      start(Wave::Sine, kE5, kE5, 0.12F, 0.40F, 0.11F);
      start(Wave::Sine, kG5, kG5, 0.12F, 0.40F, 0.22F);
      start(Wave::Sine, kC6, kC6, 0.12F, 0.40F, 0.33F);
      start(Wave::Sine, kG6, kG6, 0.45F, 0.42F, 0.44F);
      break;
    case Sound::Promotion:
      start(Wave::Triangle, kC6, kC6, 0.14F, 0.40F, 0.95F);
      start(Wave::Triangle, kE6, kE6, 0.14F, 0.40F, 1.08F);
      start(Wave::Triangle, kG6, kG6, 0.60F, 0.45F, 1.21F);
      break;
    case Sound::TiltWarning:
      start(Wave::Square, 220.0F, 220.0F, 0.10F, 0.28F);
      start(Wave::Square, 220.0F, 220.0F, 0.10F, 0.28F, 0.16F);
      break;
    case Sound::Tilt:
      start(Wave::Square, 95.0F, 60.0F, 0.80F, 0.40F);
      break;
    case Sound::GameOver:
      start(Wave::Triangle, kC5, kC5, 0.25F, 0.40F);
      start(Wave::Triangle, kC5 * 0.84F, kC5 * 0.84F, 0.25F, 0.40F, 0.24F);
      start(Wave::Triangle, kC5 * 0.667F, kC5 * 0.667F, 0.25F, 0.40F, 0.48F);
      start(Wave::Triangle, kC5 * 0.5F, kC5 * 0.5F, 0.70F, 0.45F, 0.72F);
      break;
    case Sound::Nudge:
      start(Wave::Sine, 80.0F, 45.0F, 0.12F, 0.60F);
      break;
  }
}

void SoundMixer::trigger(SoundSet sounds) noexcept {
  for (std::size_t i = 0; i < kSoundCount; ++i) {
    const Sound sound = static_cast<Sound>(i);
    if ((sounds & soundBit(sound)) != 0U && holdoff_[i] == 0) {
      startSound(sound);
      holdoff_[i] = kRetriggerSamples;
    }
  }
}

std::size_t SoundMixer::activeVoices() const noexcept {
  return static_cast<std::size_t>(
      std::count_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.active; }));
}

void SoundMixer::render(float* out, std::size_t frames) noexcept {
  if (out == nullptr) {
    return;
  }
  for (std::uint32_t& wait : holdoff_) {
    wait = frames >= wait ? 0U : wait - static_cast<std::uint32_t>(frames);
  }
  const float rate = static_cast<float>(kSampleRate);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    float mix = music_.next();
    for (Voice& v : voices_) {
      if (!v.active) {
        continue;
      }
      if (v.delay > 0) {
        --v.delay;
        continue;
      }
      const float t = static_cast<float>(v.position) / static_cast<float>(v.length);
      const float hz = v.startHz + (v.endHz - v.startHz) * t;

      float sample = 0.0F;
      switch (v.wave) {
        case Wave::Sine:
          sample = std::sin(kTwoPi * v.phase);
          break;
        case Wave::Square:
          sample = v.phase < 0.5F ? 0.6F : -0.6F;
          break;
        case Wave::Triangle:
          sample = 4.0F * std::fabs(v.phase - 0.5F) - 1.0F;
          break;
        case Wave::Noise:
          noiseState_ = noiseState_ * 1664525U + 1013904223U;
          sample = static_cast<float>(noiseState_ >> 8U) / 8388608.0F - 1.0F;
          break;
      }
      v.phase += hz / rate;
      v.phase -= std::floor(v.phase);

      // Envelope: quick fade in, decay across the note, short fade out.
      float envelope = (1.0F - t) * (1.0F - t);
      if (v.position < kFadeIn) {
        envelope *= static_cast<float>(v.position) / static_cast<float>(kFadeIn);
      }
      const std::uint32_t left = v.length - v.position;
      if (left < kFadeOut) {
        envelope *= static_cast<float>(left) / static_cast<float>(kFadeOut);
      }
      mix += sample * envelope * v.gain;

      if (++v.position >= v.length) {
        v.active = false;
      }
    }
    // Soft limit, so many simultaneous voices cannot clip.
    // Mute fades over a few milliseconds, so it never cuts mid-wave.
    muteGain_ += muted_ ? -kMuteStep : kMuteStep;
    muteGain_ = std::clamp(muteGain_, 0.0F, 1.0F);
    out[frame] = std::tanh(mix * kMasterGain) * muteGain_;
  }
}

}  // namespace pinball
