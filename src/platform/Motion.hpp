#pragma once

#include "platform/MacMenu.hpp"
#include "platform/ios/IosSystem.hpp"

namespace pinball {

// The system's Reduce Motion setting, wherever the game can read it: iOS
// (UIAccessibilityIsReduceMotionEnabled) and macOS (NSWorkspace). False
// elsewhere. The App reads it at start and on every return to the
// foreground and keeps it (App::reduceMotion); everything that moves for
// show (the title's animation, screen transitions, the table's jolt, an
// attract light show) asks the App, not this.
[[nodiscard]] inline bool systemReduceMotion() noexcept {
  return ios::reduceMotionEnabled() || macReduceMotionEnabled();
}

}  // namespace pinball
