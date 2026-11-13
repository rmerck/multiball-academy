// Compiled for iOS only (CMakeLists.txt). Not written yet: these give the
// same answers as the inline versions other systems get.
#include "platform/ios/IosSystem.hpp"

namespace pinball::ios {

bool reduceMotionEnabled() noexcept { return false; }

bool otherAudioPlaying() noexcept { return false; }

void tap(float /*strength*/) noexcept {}

}  // namespace pinball::ios
