#pragma once

// What the game asks of iOS itself, beyond SDL. Off iOS every question is
// answered "no" and nothing is done, so callers need no conditions of their
// own. (PINBALL_IOS is defined by the build for every file when the target
// is iOS.)
namespace pinball::ios {

#if defined(PINBALL_IOS)
// The system's Reduce Motion setting is on.
bool reduceMotionEnabled() noexcept;
// Another application's audio is playing: the game then starts without its
// tune.
bool otherAudioPlaying() noexcept;
// A short tap of the device's haptics, strength 0 to 1. Does nothing where
// there are none.
void tap(float strength) noexcept;
#else
inline bool reduceMotionEnabled() noexcept { return false; }
inline bool otherAudioPlaying() noexcept { return false; }
inline void tap(float /*strength*/) noexcept {}
#endif

}  // namespace pinball::ios
