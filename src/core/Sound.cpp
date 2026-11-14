#include "core/Sound.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace pinball {

namespace {

constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
static_assert(kTwoPi == 6.28318530717958647692F);
constexpr float kMasterGain = 0.55F;
// Fade in and out, in samples, so no voice starts or stops with a click.
constexpr std::uint32_t kFadeIn = 96;
constexpr std::uint32_t kFadeOut = 480;
// Mute and unmute ramp over 5 ms.
constexpr float kMuteStep = 1.0F / 240.0F;
// A replaced voice's last sample dies away by this factor per sample: to a
// thousandth in about 14 ms. Slow enough not to click, too short to hear as
// a sound of its own.
constexpr float kCutTailDecay = 0.99F;
// Below this the tail is dropped, which also keeps it out of the slow
// denormal range.
constexpr float kCutTailSilent = 1.0e-5F;

// Musical notes, in Hz.
constexpr float kA4 = 440.0F;
constexpr float kC5 = 523.25F;
constexpr float kD5 = 587.33F;
constexpr float kEb5 = 622.25F;
constexpr float kE5 = 659.25F;
constexpr float kG5 = 783.99F;
constexpr float kA5 = 880.0F;
constexpr float kB5 = 987.77F;
constexpr float kC6 = 1046.5F;
constexpr float kD6 = 1174.7F;
constexpr float kE6 = 1318.5F;
constexpr float kG6 = 1568.0F;
constexpr float kC7 = 2093.0F;

// The noise generator's state as a level in [-1, 1).
constexpr float noiseLevel(std::uint32_t state) noexcept {
  return static_cast<float>(state >> 8U) / 8388608.0F - 1.0F;
}

}  // namespace

void SoundMixer::start(Wave wave, float startHz, float endHz, float seconds, float gain,
                       float delaySeconds) noexcept {
  // Reuse a free voice, or failing that the one with the least left to play,
  // counting any wait before it starts. Voices of the sound being started
  // are passed over, so a sound cannot replace its own first notes with its
  // later ones.
  Voice* chosen = nullptr;
  std::uint32_t leastLeft = 0;
  for (Voice& v : voices_) {
    if (!v.active) {
      chosen = &v;
      break;
    }
    if (v.batch == batch_) {
      continue;
    }
    const std::uint32_t left = v.delay + (v.length - std::min(v.position, v.length));
    if (chosen == nullptr || left < leastLeft) {
      chosen = &v;
      leastLeft = left;
    }
  }
  if (chosen == nullptr) {
    return;  // Only if one sound had more parts than there are voices.
  }
  const float rate = static_cast<float>(kSampleRate);
  Voice& v = *chosen;
  if (v.active) {
    // Carry on from the replaced voice's last sample, so the mix does not jump.
    cutTail_ += v.last;
  }
  const float startStep = std::clamp(startHz, 20.0F, 12000.0F) / rate;
  v.active = true;
  v.wave = wave;
  v.startStep = startStep;
  v.stepGlide = std::clamp(endHz, 20.0F, 12000.0F) / rate - startStep;
  v.gain = std::clamp(gain, 0.0F, 1.0F);
  v.delay = static_cast<std::uint32_t>(std::clamp(delaySeconds, 0.0F, 5.0F) * rate);
  v.length = std::max<std::uint32_t>(
      static_cast<std::uint32_t>(std::clamp(seconds, 0.005F, 5.0F) * rate), kFadeIn + kFadeOut);
  v.position = 0;
  v.perLength = 1.0F / static_cast<float>(v.length);
  v.phase = 0.0F;
  v.last = 0.0F;
  v.held = 0.0F;
  v.target = 0.0F;
  v.batch = batch_;
}

void SoundMixer::startNoise(float seconds, float gain) noexcept {
  start(Wave::Noise, 20.0F, 20.0F, seconds, gain);
}

void SoundMixer::startSweptNoise(float startHz, float endHz, float seconds, float gain) noexcept {
  // It starts level and silent; the first random level comes a cycle in.
  start(Wave::SweptNoise, startHz, endHz, seconds, gain);
}

void SoundMixer::startSound(Sound sound) noexcept {
  ++batch_;
  switch (sound) {
    case Sound::Flipper:
      start(Wave::Triangle, 150.0F, 70.0F, 0.06F, 0.55F);
      startNoise(0.02F, 0.20F);
      break;
    case Sound::Launch:
      // A whoosh that brightens as the ball leaves.
      startSweptNoise(900.0F, 4000.0F, 0.28F, 0.30F);
      start(Wave::Sine, 110.0F, 330.0F, 0.25F, 0.35F);
      break;
    case Sound::Bumper:
      start(Wave::Sine, 620.0F, 210.0F, 0.11F, 0.60F);
      startNoise(0.025F, 0.25F);
      break;
    case Sound::Slingshot:
      start(Wave::Square, 330.0F, 160.0F, 0.07F, 0.30F);
      startNoise(0.04F, 0.30F);
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
      startNoise(0.05F, 0.20F);
      break;
    case Sound::Spinner:
      // A light tick for each turn of the blade, so a fast spin rattles.
      start(Wave::Square, 1900.0F, 1500.0F, 0.035F, 0.30F);
      startNoise(0.015F, 0.18F);
      break;
    case Sound::Kickback:
      // A thump from below and the ball rushing back up the lane.
      start(Wave::Sine, 70.0F, 240.0F, 0.20F, 0.60F);
      startSweptNoise(500.0F, 3500.0F, 0.30F, 0.30F);
      break;
    case Sound::DoorsDrop:
      // Three doors clacking down one after another, and the bank settling.
      start(Wave::Square, 360.0F, 220.0F, 0.05F, 0.32F);
      start(Wave::Square, 330.0F, 200.0F, 0.05F, 0.32F, 0.06F);
      start(Wave::Square, 300.0F, 180.0F, 0.05F, 0.32F, 0.12F);
      startNoise(0.04F, 0.22F);
      start(Wave::Sine, 120.0F, 55.0F, 0.25F, 0.45F, 0.16F);
      break;
    case Sound::Drain:
      start(Wave::Triangle, kG5, kG5, 0.18F, 0.40F);
      start(Wave::Triangle, kE5, kE5, 0.18F, 0.40F, 0.16F);
      start(Wave::Triangle, kC5 * 0.5F, kC5 * 0.5F, 0.40F, 0.45F, 0.32F);
      break;
    case Sound::TiltWarning:
      start(Wave::Square, 220.0F, 220.0F, 0.10F, 0.28F);
      start(Wave::Square, 220.0F, 220.0F, 0.10F, 0.28F, 0.16F);
      break;
    case Sound::Tilt:
      start(Wave::Square, 95.0F, 60.0F, 0.80F, 0.40F);
      break;
    case Sound::SkillShot:
      // A quick run up the major chord to a ringing top note.
      start(Wave::Square, kC6, kC6, 0.06F, 0.20F);
      start(Wave::Square, kE6, kE6, 0.06F, 0.20F, 0.05F);
      start(Wave::Square, kG6, kG6, 0.06F, 0.20F, 0.10F);
      start(Wave::Sine, kC7, kC7, 0.30F, 0.35F, 0.15F);
      break;
    case Sound::SetComplete:
      start(Wave::Sine, kE5, kE5, 0.09F, 0.38F);
      start(Wave::Sine, kG5, kG5, 0.09F, 0.38F, 0.08F);
      start(Wave::Sine, kE6, kE6, 0.22F, 0.38F, 0.16F);
      break;
    case Sound::Lit:
      // Two bell strokes, a fourth apart: something waits to be collected.
      start(Wave::Triangle, kA5, kA5, 0.25F, 0.40F);
      start(Wave::Sine, kA5 * 2.0F, kA5 * 2.0F, 0.12F, 0.12F);
      start(Wave::Triangle, kD6, kD6, 0.35F, 0.40F, 0.14F);
      start(Wave::Sine, kD6 * 2.0F, kD6 * 2.0F, 0.15F, 0.12F, 0.14F);
      break;
    case Sound::Combo:
      // A whip upwards: the chain goes on.
      start(Wave::Sine, 500.0F, 1700.0F, 0.12F, 0.40F);
      start(Wave::Triangle, 1000.0F, 3400.0F, 0.10F, 0.15F);
      start(Wave::Sine, kG6, kG6, 0.10F, 0.25F, 0.11F);
      break;
    case Sound::CourseStart:
      start(Wave::Triangle, kC5, kC5, 0.10F, 0.40F);
      start(Wave::Triangle, kG5, kG5, 0.10F, 0.40F, 0.10F);
      start(Wave::Triangle, kC6, kC6, 0.25F, 0.40F, 0.20F);
      break;
    case Sound::CoursePassed:
      start(Wave::Sine, kC5, kC5, 0.12F, 0.40F);
      start(Wave::Sine, kE5, kE5, 0.12F, 0.40F, 0.11F);
      start(Wave::Sine, kG5, kG5, 0.12F, 0.40F, 0.22F);
      start(Wave::Sine, kC6, kC6, 0.12F, 0.40F, 0.33F);
      start(Wave::Sine, kG6, kG6, 0.45F, 0.42F, 0.44F);
      break;
    case Sound::CourseFailed:
      // Down a minor third and a third more, slowly: time is up.
      start(Wave::Triangle, kG5, kG5, 0.20F, 0.38F);
      start(Wave::Triangle, kEb5, kEb5, 0.20F, 0.38F, 0.20F);
      start(Wave::Triangle, kC5, kC5 * 0.97F, 0.55F, 0.40F, 0.40F);
      break;
    case Sound::RankUp:
      // A fanfare that waits for the course's own run to finish.
      start(Wave::Triangle, kC6, kC6, 0.14F, 0.40F, 0.95F);
      start(Wave::Triangle, kE6, kE6, 0.14F, 0.40F, 1.08F);
      start(Wave::Triangle, kG6, kG6, 0.60F, 0.45F, 1.21F);
      break;
    case Sound::BallLocked:
      // A heavy clunk as the bay closes, then two rising notes.
      start(Wave::Sine, 180.0F, 50.0F, 0.22F, 0.65F);
      startNoise(0.03F, 0.25F);
      start(Wave::Triangle, kA4, kA4, 0.12F, 0.40F, 0.18F);
      start(Wave::Triangle, kE5, kE5, 0.30F, 0.40F, 0.30F);
      break;
    case Sound::MultiballStart:
      // Three rising sirens and a held chord: every ball is coming.
      start(Wave::Square, 330.0F, 880.0F, 0.22F, 0.24F);
      start(Wave::Square, 392.0F, 1046.5F, 0.22F, 0.24F, 0.24F);
      start(Wave::Square, 440.0F, 1318.5F, 0.22F, 0.24F, 0.48F);
      start(Wave::Triangle, kC5, kC5, 0.70F, 0.30F, 0.72F);
      start(Wave::Triangle, kE5, kE5, 0.70F, 0.30F, 0.72F);
      start(Wave::Triangle, kG5, kG5, 0.70F, 0.30F, 0.72F);
      start(Wave::Sine, kC6, kC6, 0.70F, 0.30F, 0.72F);
      break;
    case Sound::Jackpot:
      // Bright and fast: the shot that matters in a multiball.
      start(Wave::Square, kC6, kC6, 0.07F, 0.22F);
      start(Wave::Square, kE6, kE6, 0.07F, 0.22F, 0.06F);
      start(Wave::Square, kG6, kG6, 0.07F, 0.22F, 0.12F);
      start(Wave::Square, kC7, kC7, 0.07F, 0.22F, 0.18F);
      start(Wave::Sine, kC6, kC6, 0.45F, 0.40F, 0.24F);
      start(Wave::Sine, kG6, kG6, 0.45F, 0.25F, 0.24F);
      break;
    case Sound::SuperJackpot:
      // The jackpot's run twice over, a sweep, and a long chord.
      start(Wave::Square, kC6, kC6, 0.07F, 0.22F);
      start(Wave::Square, kE6, kE6, 0.07F, 0.22F, 0.06F);
      start(Wave::Square, kG6, kG6, 0.07F, 0.22F, 0.12F);
      start(Wave::Square, kC7, kC7, 0.07F, 0.22F, 0.18F);
      start(Wave::Square, kC6, kC6, 0.07F, 0.22F, 0.30F);
      start(Wave::Square, kE6, kE6, 0.07F, 0.22F, 0.36F);
      start(Wave::Square, kG6, kG6, 0.07F, 0.22F, 0.42F);
      start(Wave::Square, kC7, kC7, 0.07F, 0.22F, 0.48F);
      start(Wave::Sine, 300.0F, 2400.0F, 0.40F, 0.30F, 0.54F);
      start(Wave::Triangle, kC5, kC5, 0.90F, 0.32F, 0.60F);
      start(Wave::Triangle, kG5, kG5, 0.90F, 0.32F, 0.60F);
      start(Wave::Sine, kE6, kE6, 0.90F, 0.30F, 0.60F);
      break;
    case Sound::ExtraBall:
      start(Wave::Sine, kC5, kC5, 0.10F, 0.40F);
      start(Wave::Sine, kE5, kE5, 0.10F, 0.40F, 0.09F);
      start(Wave::Sine, kG5, kG5, 0.10F, 0.40F, 0.18F);
      start(Wave::Sine, kC6, kC6, 0.30F, 0.45F, 0.27F);
      break;
    case Sound::ExtraCredit:
      // A wobble between two notes while the award is picked, then the pick.
      start(Wave::Triangle, kB5, kB5, 0.06F, 0.30F);
      start(Wave::Triangle, kD6, kD6, 0.06F, 0.30F, 0.07F);
      start(Wave::Triangle, kB5, kB5, 0.06F, 0.30F, 0.14F);
      start(Wave::Triangle, kD6, kD6, 0.06F, 0.30F, 0.21F);
      start(Wave::Sine, kG6, kG6, 0.35F, 0.40F, 0.28F);
      break;
    case Sound::Overdrive:
      // An engine winding up.
      startSweptNoise(250.0F, 2500.0F, 0.60F, 0.25F);
      start(Wave::Square, 82.0F, 330.0F, 0.60F, 0.28F);
      start(Wave::Sine, kD5, kD6, 0.40F, 0.25F, 0.30F);
      break;
    case Sound::BallSaved:
      start(Wave::Sine, kC5, kC5, 0.10F, 0.35F);
      start(Wave::Sine, kG5, kG5, 0.16F, 0.35F, 0.10F);
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
  return static_cast<std::size_t>(std::ranges::count(voices_, true, &Voice::active));
}

void SoundMixer::render(std::span<float> out) noexcept {
  const std::size_t frames = out.size();
  for (std::uint32_t& wait : holdoff_) {
    wait = frames >= wait ? 0U : wait - static_cast<std::uint32_t>(frames);
  }
  if (!busy() && cutTail_ == 0.0F) {
    // Nothing is sounding and nothing can start during this call, so the
    // output is silence. Only the mute ramp still moves.
    for (float& sample : out) {
      muteGain_ = std::clamp(muteGain_ + (muted_ ? -kMuteStep : kMuteStep), 0.0F, 1.0F);
      sample = 0.0F;
    }
    return;
  }
  constexpr float kPerFadeIn = 1.0F / static_cast<float>(kFadeIn);
  constexpr float kPerFadeOut = 1.0F / static_cast<float>(kFadeOut);
  for (float& output : out) {
    float mix = music_.next();
    if (cutTail_ != 0.0F) {
      mix += cutTail_;
      cutTail_ *= kCutTailDecay;
      if (std::fabs(cutTail_) < kCutTailSilent) {
        cutTail_ = 0.0F;
      }
    }
    for (Voice& v : voices_) {
      if (!v.active) {
        continue;
      }
      if (v.delay > 0) {
        --v.delay;
        continue;
      }
      const float t = static_cast<float>(v.position) * v.perLength;

      float sample = 0.0F;
      if (v.wave == Wave::SweptNoise) {
        // Kept out of the switch: a fifth case turns it into a jump table,
        // which costs every voice about a fifth more time (measured, GCC 13).
        // The generator still moves once a sample, as for white noise, so
        // every other noise voice gets the numbers it always did.
        noiseState_ = noiseState_ * 1664525U + 1013904223U;
        sample = v.held + (v.target - v.held) * v.phase;
      } else {
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
            sample = noiseLevel(noiseState_);
            break;
          case Wave::SweptNoise:
            break;  // Handled above.
        }
      }
      // The step is at most a quarter cycle, so one subtraction wraps it.
      v.phase += v.startStep + v.stepGlide * t;
      if (v.phase >= 1.0F) {
        v.phase -= 1.0F;
        if (v.wave == Wave::SweptNoise) {
          v.held = v.target;
          v.target = noiseLevel(noiseState_);
        }
      }

      // Envelope: quick fade in, decay across the note, short fade out.
      float envelope = (1.0F - t) * (1.0F - t);
      if (v.position < kFadeIn) {
        envelope *= static_cast<float>(v.position) * kPerFadeIn;
      }
      const std::uint32_t left = v.length - v.position;
      if (left < kFadeOut) {
        envelope *= static_cast<float>(left) * kPerFadeOut;
      }
      v.last = sample * envelope * v.gain;
      mix += v.last;

      if (++v.position >= v.length) {
        v.active = false;
      }
    }
    // Soft limit, so many simultaneous voices cannot clip.
    // Mute fades over a few milliseconds, so it never cuts mid-wave.
    muteGain_ += muted_ ? -kMuteStep : kMuteStep;
    muteGain_ = std::clamp(muteGain_, 0.0F, 1.0F);
    output = std::tanh(mix * kMasterGain) * muteGain_;
  }
}

}  // namespace pinball
