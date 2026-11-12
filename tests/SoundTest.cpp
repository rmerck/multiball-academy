#include <cmath>
#include <cstddef>
#include <vector>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/Game.hpp"
#include "core/Sound.hpp"

namespace {

using pinball::Sound;
using pinball::soundBit;
using pinball::SoundMixer;
using pinball::SoundSet;

struct Stats {
  float peak = 0.0F;
  double energy = 0.0;
  bool finite = true;
};

Stats render(SoundMixer& mixer, std::size_t frames) {
  std::vector<float> buffer(frames, 123.0F);
  mixer.render(buffer.data(), frames);
  Stats stats;
  for (const float sample : buffer) {
    stats.finite = stats.finite && std::isfinite(sample);
    stats.peak = std::fmax(stats.peak, std::fabs(sample));
    stats.energy += static_cast<double>(sample) * static_cast<double>(sample);
  }
  return stats;
}

}  // namespace

void testSound() {
  {  // A sound repeated at once does not stack; after the hold-off it starts again.
    SoundMixer mixer;
    mixer.trigger(soundBit(Sound::Nudge));
    const std::size_t one = mixer.activeVoices();
    CHECK(one > 0);
    mixer.trigger(soundBit(Sound::Nudge));
    CHECK(mixer.activeVoices() == one);
    render(mixer, SoundMixer::kRetriggerSamples - 1);
    mixer.trigger(soundBit(Sound::Nudge));
    CHECK(mixer.activeVoices() == one);
    render(mixer, 1);
    mixer.trigger(soundBit(Sound::Nudge));
    CHECK(mixer.activeVoices() == 2 * one);
    // A different sound is not held off.
    mixer.trigger(soundBit(Sound::Bumper));
    CHECK(mixer.activeVoices() > 2 * one);
  }

  {  // Silence when nothing is playing, and the buffer is always written.
    SoundMixer mixer;
    const Stats stats = render(mixer, 4800);
    CHECK(stats.peak == 0.0F);
    CHECK(mixer.activeVoices() == 0);
  }

  // Every sound is audible, stays inside [-1, 1], and ends by itself.
  for (std::size_t i = 0; i < pinball::kSoundCount; ++i) {
    SoundMixer mixer;
    mixer.trigger(SoundSet{1} << i);
    CHECK(mixer.activeVoices() > 0);
    const Stats first = render(mixer, static_cast<std::size_t>(SoundMixer::kSampleRate) * 2);
    CHECK(first.finite);
    CHECK(first.peak <= 1.0F);
    CHECK(first.energy > 1.0);
    render(mixer, static_cast<std::size_t>(SoundMixer::kSampleRate) * 3);
    CHECK(mixer.activeVoices() == 0);
    CHECK(render(mixer, 480).peak == 0.0F);
  }

  {  // Unknown bits are ignored.
    SoundMixer mixer;
    mixer.trigger(~SoundSet{0} << pinball::kSoundCount);
    CHECK(mixer.activeVoices() == 0);
  }

  {  // Flooding it never overruns the voice pool or the output range.
    SoundMixer mixer;
    float peak = 0.0F;
    bool finite = true;
    for (int i = 0; i < 2000; ++i) {
      mixer.trigger(~SoundSet{0});
      CHECK(mixer.activeVoices() <= SoundMixer::kVoices);
      const Stats stats = render(mixer, 64);
      peak = std::fmax(peak, stats.peak);
      finite = finite && stats.finite;
    }
    CHECK(finite);
    CHECK(peak <= 1.0F);
    CHECK(peak > 0.1F);
  }

  {  // Mute produces silence without stopping the voices' clocks.
    SoundMixer mixer;
    mixer.setMuted(true);
    render(mixer, 480);  // The mute fades in over 5 ms.
    mixer.trigger(soundBit(Sound::Bumper));
    CHECK(render(mixer, 480).peak == 0.0F);
    mixer.setMuted(false);
    CHECK(render(mixer, 480).peak > 0.0F);
  }

  {  // Degenerate requests are safe.
    SoundMixer mixer;
    mixer.trigger(soundBit(Sound::Target));
    mixer.render(nullptr, 100);
    float one = 9.0F;
    mixer.render(&one, 0);
    CHECK(one == 9.0F);
  }

  {  // A sound that starts with every voice busy keeps all of its own parts.
    SoundMixer mixer;
    const SoundSet longOnes = soundBit(Sound::Promotion) | soundBit(Sound::GameOver) |
                              soundBit(Sound::Tilt) | soundBit(Sound::HoleIn);
    mixer.trigger(longOnes);  // 9 voices.
    render(mixer, SoundMixer::kRetriggerSamples);
    mixer.trigger(longOnes);  // 18.
    render(mixer, SoundMixer::kRetriggerSamples);
    mixer.trigger(soundBit(Sound::Promotion) | soundBit(Sound::Tilt) | soundBit(Sound::HoleIn));
    render(mixer, SoundMixer::kRetriggerSamples);
    mixer.trigger(soundBit(Sound::Tilt));
    CHECK(mixer.activeVoices() == SoundMixer::kVoices);
    // The flipper's thump and its click each replace some other voice. Both
    // are over within 70 ms, and nothing else ends that soon.
    mixer.trigger(soundBit(Sound::Flipper));
    CHECK(mixer.activeVoices() == SoundMixer::kVoices);
    render(mixer, 3360);
    CHECK(mixer.activeVoices() == SoundMixer::kVoices - 2);
  }

  {  // Replacing a sounding voice does not make the output jump. Two copies of
     // a mixer with a full pool; one then has voices replaced. Its next sample
     // still matches the other's, because new voices fade in from nothing and
     // what was cut off dies away from where it stood. Sounds with noise in
     // them are left out: noise jumps from sample to sample by itself.
    const SoundSet tonal =
        ~(soundBit(Sound::Flipper) | soundBit(Sound::Launch) | soundBit(Sound::Bumper) |
          soundBit(Sound::Slingshot) | soundBit(Sound::HoleOut));
    SoundMixer crowded;
    float largestCut = 0.0F;
    for (int round = 0; round < 40; ++round) {
      crowded.trigger(tonal);
      render(crowded, SoundMixer::kRetriggerSamples);
      SoundMixer before = crowded;
      SoundMixer after = crowded;
      after.trigger(tonal);
      float kept = 0.0F;
      float cut = 0.0F;
      before.render(&kept, 1);
      after.render(&cut, 1);
      largestCut = std::fmax(largestCut, std::fabs(cut - kept));
    }
    CHECK(largestCut < 0.02F);
  }

  {  // The same triggers give the same samples.
    SoundMixer a;
    SoundMixer b;
    a.trigger(soundBit(Sound::MissionComplete) | soundBit(Sound::Slingshot));
    b.trigger(soundBit(Sound::MissionComplete) | soundBit(Sound::Slingshot));
    std::vector<float> x(9600);
    std::vector<float> y(9600);
    a.render(x.data(), x.size());
    b.render(y.data(), y.size());
    CHECK(x == y);
  }

  {  // The game reports the sounds of play: over a scripted game every
     // common cue is heard, and a silent step reports nothing.
    pinball::Game game;
    SoundSet heard = 0;
    for (int i = 0; i < 120 * 600; ++i) {
      pinball::InputState input;
      input.plunger = (i % 900) < 100 + (i / 900) % 60;
      input.leftFlipper = (i / 29) % 2 == 0;
      input.rightFlipper = (i / 41) % 2 == 0;
      input.newGame = game.gameOver();
      game.step(input);
      heard |= game.sounds();
    }
    for (const Sound sound : {Sound::Flipper, Sound::Launch, Sound::Bumper, Sound::Slingshot,
                              Sound::Target, Sound::Rollover, Sound::HoleIn, Sound::HoleOut,
                              Sound::Drain, Sound::GameOver}) {
      CHECK((heard & soundBit(sound)) != 0U);
    }

    pinball::Game idle;
    idle.step(pinball::InputState{});
    idle.step(pinball::InputState{});
    CHECK(idle.sounds() == 0U);
  }
}
