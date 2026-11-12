#pragma once

#include <cstdint>

namespace pinball {

// The main loop's decisions that do not need a window, kept here so they can
// be tested: how much time the simulation is given across a pause, and when
// a halted loop may sleep.

// Turns each frame's wall time into the time the simulation should see.
class PauseClock {
 public:
  // frameSeconds is the time since the previous frame; halted is whether the
  // simulation stands still this frame. Returns nothing while halted, and
  // nothing for the first frame after a halt either: that frame's interval
  // covers the wait, and would otherwise arrive as a burst of catch-up steps.
  constexpr double simulated(double frameSeconds, bool halted) noexcept {
    const bool skip = halted || wasHalted_;
    wasHalted_ = halted;
    return skip ? 0.0 : frameSeconds;
  }

 private:
  bool wasHalted_ = false;
};

// Longest a halted loop stays awake for sound, in frames: beyond any effect's
// tail.
inline constexpr std::uint32_t kMaxHaltedBusyFrames = 600;

// True when the loop should sleep until an event arrives instead of
// redrawing a still picture at full rate.
//   halted        the simulation stands still (pause, About, a prompt)
//   timedRun      an automated run that must count frames at full rate
//   soundAudible  something is still playing through an open audio device;
//                 its tail needs the loop at frame rate to play without gaps
//   haltedFrames  frames spent halted so far, this one included
// A sound that outlasts kMaxHaltedBusyFrames is taken to be a device that
// stopped accepting audio, and the loop sleeps anyway.
constexpr bool sleepsWhileHalted(bool halted, bool timedRun, bool soundAudible,
                                 std::uint32_t haltedFrames) noexcept {
  return halted && !timedRun && (!soundAudible || haltedFrames > kMaxHaltedBusyFrames);
}

}  // namespace pinball
