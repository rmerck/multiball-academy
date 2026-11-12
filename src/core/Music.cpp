#include "core/Music.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace pinball {

namespace {

constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
static_assert(kTwoPi == 6.28318530717958647692F);
// Fade in or out over 50 ms.
constexpr float kFadeStep = 1.0F / (0.050F * static_cast<float>(Music::kSampleRate));
// Each note rises over 2 ms, so no note starts with a click.
constexpr float kAttackStep = 1.0F / (0.002F * static_cast<float>(Music::kSampleRate));
// Envelope multipliers per sample: the note falls to a thousandth of its
// level in 0.30 s, 0.18 s and 0.70 s.
constexpr float kBassDecay = 0.99952041F;
constexpr float kArpeggioDecay = 0.99920080F;
constexpr float kLeadDecay = 0.99979443F;
// Below this a note is treated as finished, which also keeps the envelope
// out of the slow denormal range.
constexpr float kSilent = 1.0e-4F;

constexpr float kBassGain = 0.50F;
constexpr float kArpeggioGain = 0.20F;
constexpr float kLeadGain = 0.30F;
constexpr float kOverallGain = Music::kPeak / (kBassGain + kArpeggioGain + kLeadGain);

// Notes are MIDI numbers: 69 is the A above middle C, 12 to the octave.
constexpr int kOctave = 12;

// One chord per bar: A minor, F, C, G, A minor, F, D minor, E. The bass sits
// high enough (82 Hz and up) to be heard on small speakers.
struct Chord {
  int bass;
  std::array<int, 3> tones;
};
constexpr std::array<Chord, Music::kBars> kChords{{
    {45, {57, 60, 64}},
    {41, {53, 57, 60}},
    {48, {55, 60, 64}},
    {43, {55, 59, 62}},
    {45, {57, 60, 64}},
    {41, {53, 57, 60}},
    {50, {57, 62, 65}},
    {40, {56, 59, 64}},
}};

// Bass rhythm over the sixteen steps of a bar: 0 rest, 1 root, 2 root an
// octave up.
constexpr std::array<std::uint8_t, Music::kStepsPerBar> kBassRhythm{
    1, 0, 0, 1, 0, 0, 1, 0, 2, 0, 0, 1, 0, 0, 2, 0};
// Arpeggio: which chord tone on each step. 3 is the lowest tone an octave up.
constexpr std::array<std::uint8_t, Music::kStepsPerBar> kArpeggio{
    0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1, 0, 2, 1, 3};
// Lead: eight eighth notes per bar, 0 for no new note.
constexpr std::array<std::array<std::uint8_t, 8>, Music::kBars> kLead{{
    {76, 0, 0, 74, 72, 0, 69, 0},
    {72, 0, 0, 69, 65, 0, 69, 0},
    {67, 0, 72, 0, 76, 0, 74, 72},
    {74, 0, 0, 71, 67, 0, 0, 0},
    {76, 0, 0, 74, 72, 0, 76, 0},
    {77, 0, 0, 76, 72, 0, 69, 0},
    {74, 0, 77, 0, 81, 0, 77, 74},
    {76, 0, 0, 68, 71, 0, 76, 0},
}};

float triangle(float phase) noexcept { return 4.0F * std::fabs(phase - 0.5F) - 1.0F; }

}  // namespace

void Music::strike(Tone& tone, int midiNote, float decay) noexcept {
  const float hz = 440.0F * std::exp2(static_cast<float>(midiNote - 69) / 12.0F);
  tone.increment = hz / static_cast<float>(kSampleRate);
  tone.decay = decay;
  // The envelope rises from wherever it is, and the phase carries on, so a
  // new note never jumps.
  tone.attacking = true;
}

float Music::advance(Tone& tone) noexcept {
  if (tone.attacking) {
    tone.level += kAttackStep;
    if (tone.level >= 1.0F) {
      tone.level = 1.0F;
      tone.attacking = false;
    }
  } else {
    tone.level *= tone.decay;
    if (tone.level < kSilent) {
      tone.level = 0.0F;
    }
  }
  tone.phase += tone.increment;
  if (tone.phase >= 1.0F) {
    tone.phase -= 1.0F;
  }
  return tone.level;
}

void Music::startStep() noexcept {
  const std::size_t bar = (step_ / kStepsPerBar) % kBars;
  const std::size_t inBar = step_ % kStepsPerBar;
  const Chord& chord = kChords[bar];

  if (kBassRhythm[inBar] != 0) {
    strike(bass_, chord.bass + (kBassRhythm[inBar] == 2 ? kOctave : 0), kBassDecay);
  }
  const std::size_t tone = kArpeggio[inBar];
  strike(arpeggio_, tone < chord.tones.size() ? chord.tones[tone] : chord.tones[0] + kOctave,
         kArpeggioDecay);
  if (inBar % 2 == 0) {
    const int note = kLead[bar][inBar / 2];
    if (note != 0) {
      strike(lead_, note, kLeadDecay);
    }
  }
}

float Music::next() noexcept {
  if (!playing_ && gain_ <= 0.0F) {
    return 0.0F;
  }
  gain_ += playing_ ? kFadeStep : -kFadeStep;
  if (gain_ > 1.0F) {
    gain_ = 1.0F;
  } else if (gain_ < 0.0F) {
    gain_ = 0.0F;
  }

  if (sampleInStep_ == 0) {
    startStep();
  }
  // Phases are read before they advance.
  const float bassWave = triangle(bass_.phase);
  const float arpeggioWave = triangle(arpeggio_.phase);
  const float leadWave = std::sin(kTwoPi * lead_.phase);
  const float mix = bassWave * advance(bass_) * kBassGain +
                    arpeggioWave * advance(arpeggio_) * kArpeggioGain +
                    leadWave * advance(lead_) * kLeadGain;

  if (++sampleInStep_ >= kStepSamples) {
    sampleInStep_ = 0;
    step_ = (step_ + 1) % (kStepsPerBar * kBars);
  }
  return mix * gain_ * kOverallGain;
}

}  // namespace pinball
