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
