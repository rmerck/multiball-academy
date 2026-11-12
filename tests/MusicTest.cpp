#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/Music.hpp"
#include "core/Sound.hpp"

void testMusic() {
  using pinball::Music;
  using pinball::SoundMixer;

  {  // Stopped: silent, and it stays silent.
    Music music;
    CHECK(!music.playing() && !music.audible());
    for (int i = 0; i < 1000; ++i) {
      CHECK(music.next() == 0.0F);
    }
  }

  {  // Playing: audible, finite and inside its stated peak for two full loops.
    Music music;
    music.setPlaying(true);
    CHECK(music.audible());
    float peak = 0.0F;
    double energy = 0.0;
    bool finite = true;
    float largestJump = 0.0F;
    float previous = 0.0F;
    for (std::uint32_t i = 0; i < 2 * Music::kLoopSamples; ++i) {
      const float sample = music.next();
      finite = finite && std::isfinite(sample);
      peak = std::fmax(peak, std::fabs(sample));
      energy += static_cast<double>(sample) * static_cast<double>(sample);
      largestJump = std::fmax(largestJump, std::fabs(sample - previous));
      previous = sample;
    }
    CHECK(finite);
    CHECK(peak <= Music::kPeak);
    CHECK(peak > 0.05F);
    CHECK(energy > 100.0);
    // No clicks: neighbouring samples never differ by much.
    CHECK(largestJump < 0.08F);
  }

  {  // The tune loops: each pass carries the same energy. (The samples are
     // not identical, because oscillator phases run on across the loop.)
    Music music;
    music.setPlaying(true);
    double passes[3] = {0.0, 0.0, 0.0};
    for (double& energy : passes) {
      for (std::uint32_t i = 0; i < Music::kLoopSamples; ++i) {
        const float sample = music.next();
        energy += static_cast<double>(sample) * static_cast<double>(sample);
      }
    }
    CHECK(passes[1] > 100.0);
    CHECK(std::fabs(passes[2] - passes[1]) < 0.02 * passes[1]);
  }

  {  // Stopping fades out within 50 ms, then holds its place.
    Music music;
    music.setPlaying(true);
    for (int i = 0; i < 48000; ++i) {
      music.next();
    }
    music.setPlaying(false);
    CHECK(music.audible());
    for (int i = 0; i < 2400; ++i) {
      music.next();
    }
    CHECK(!music.audible());
    CHECK(music.next() == 0.0F);
    music.setPlaying(true);
    float peak = 0.0F;
    for (int i = 0; i < 48000; ++i) {
      peak = std::fmax(peak, std::fabs(music.next()));
    }
    CHECK(peak > 0.05F);
  }

  {  // In the mixer: off by default, heard when on, silenced by mute, and it
     // keeps the mixer busy until it has faded.
    SoundMixer mixer;
    CHECK(!mixer.musicPlaying() && !mixer.busy());
    std::vector<float> buffer(48000);
    mixer.setMusicPlaying(true);
    CHECK(mixer.busy());
    mixer.render(buffer.data(), buffer.size());
    float peak = 0.0F;
    for (const float sample : buffer) {
      peak = std::fmax(peak, std::fabs(sample));
    }
    CHECK(peak > 0.02F && peak <= 1.0F);

    mixer.setMuted(true);
    mixer.render(buffer.data(), 480);
    CHECK(buffer[0] == 0.0F && buffer[479] == 0.0F);
    mixer.setMuted(false);

    mixer.setMusicPlaying(false);
    CHECK(mixer.busy());
    mixer.render(buffer.data(), 4800);
    CHECK(!mixer.busy());
    CHECK(buffer[4799] == 0.0F);
  }
}
