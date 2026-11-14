#include "core/Feedback.hpp"

#include <cstddef>
#include <utility>

namespace pinball {

namespace {

constexpr std::uint64_t kIntervals[4] = {HapticGate::kFlipperMs, HapticGate::kHitMs,
                                         HapticGate::kDrainMs, HapticGate::kJackpotMs};

constexpr bool has(SoundSet sounds, Sound sound) noexcept {
  return (sounds & soundBit(sound)) != 0;
}

}  // namespace

std::optional<Haptic> hapticFor(SoundSet sounds) noexcept {
  if (has(sounds, Sound::Jackpot) || has(sounds, Sound::SuperJackpot) ||
      has(sounds, Sound::MultiballStart)) {
    return Haptic::Jackpot;
  }
  if (has(sounds, Sound::Drain)) {
    return Haptic::Drain;
  }
  if (has(sounds, Sound::Bumper) || has(sounds, Sound::Slingshot)) {
    return Haptic::Hit;
  }
  if (has(sounds, Sound::Flipper)) {
    return Haptic::Flipper;
  }
  return std::nullopt;
}

bool HapticGate::due(Haptic kind, std::uint64_t nowMs) const noexcept {
  const auto index = static_cast<std::size_t>(std::to_underlying(kind));
  if (playedAny_ && nowMs - lastAny_ < kAnyMs) {
    return false;
  }
  if (played_[index] && nowMs - last_[index] < kIntervals[index]) {
    return false;
  }
  if (kind == Haptic::Hit && burstHits_ >= kBurstHits && nowMs - burstStart_ < kBurstWindowMs) {
    return false;
  }
  return true;
}

std::optional<Haptic> HapticGate::take(SoundSet sounds, std::uint64_t nowMs) noexcept {
  // A clock that went back is held where it was.
  if (playedAny_ && nowMs < lastAny_) {
    nowMs = lastAny_;
  }
  // Heaviest first: the first that is called for and due is played.
  for (const Haptic kind : {Haptic::Jackpot, Haptic::Drain, Haptic::Hit, Haptic::Flipper}) {
    const std::optional<Haptic> wanted = hapticFor(sounds & [kind] {
      switch (kind) {
        case Haptic::Jackpot:
          return soundBit(Sound::Jackpot) | soundBit(Sound::SuperJackpot) |
                 soundBit(Sound::MultiballStart);
        case Haptic::Drain:
          return soundBit(Sound::Drain);
        case Haptic::Hit:
          return soundBit(Sound::Bumper) | soundBit(Sound::Slingshot);
        case Haptic::Flipper:
          return soundBit(Sound::Flipper);
      }
      return SoundSet{0};
    }());
    if (!wanted || !due(kind, nowMs)) {
      continue;
    }
    const auto index = static_cast<std::size_t>(std::to_underlying(kind));
    last_[index] = nowMs;
    played_[index] = true;
    lastAny_ = nowMs;
    playedAny_ = true;
    if (kind == Haptic::Hit) {
      if (burstHits_ == 0 || nowMs - burstStart_ >= kBurstWindowMs) {
        burstStart_ = nowMs;
        burstHits_ = 0;
      }
      ++burstHits_;
    }
    return kind;
  }
  return std::nullopt;
}

}  // namespace pinball
