#pragma once

#include <SDL3/SDL.h>

namespace pinball {

// macOS gives every application an "About" item in its menu bar that opens a
// stock system panel. This points that item at the game instead: choosing it
// pushes an SDL event of the given type. (The item titles already carry the
// game's name, which SDL takes from the application metadata.)
//
// Call after the video subsystem is up. Returns false if there was no such
// menu item, which is normal with an offscreen video driver. There is no
// menu bar on iOS, nor anywhere else but macOS.
#if defined(__APPLE__) && !defined(PINBALL_IOS)
bool routeAboutMenu(Uint32 eventType);
// The system's Reduce Motion setting (Accessibility, Display) is on.
bool macReduceMotionEnabled() noexcept;
#else
inline bool routeAboutMenu(Uint32 /*eventType*/) { return false; }
inline bool macReduceMotionEnabled() noexcept { return false; }
#endif

}  // namespace pinball
