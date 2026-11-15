#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "core/Feedback.hpp"
#include "core/ScreenLayout.hpp"

struct SDL_Window;

// What the game asks of iOS itself, beyond SDL. Off iOS every question is
// answered "no" and nothing is done, so callers need no conditions of their
// own. (PINBALL_IOS is defined by the build for every file when the target
// is iOS.)
namespace pinball::ios {

// What an accessibility element is: a control, which can be activated; a
// title; or a block of text, which is only read.
enum class AccessRole : std::uint8_t { Button, Header, Text };

// One control, title or block of text of a menu screen for VoiceOver. frame
// is in the window's points. Activating a control pushes an SDL event of the
// type given to setAccessibilityElements with user.code set to code; a title
// or text is read and not activated, and neither is a control that is not
// enabled (one drawn greyed, that leads nowhere).
struct AccessibleElement {
  std::string_view label;
  screen::Rect frame;
  std::int32_t code = 0;
  AccessRole role = AccessRole::Button;
  bool enabled = true;
  // The tab of a set that is up.
  bool selected = false;
};

// Where VoiceOver goes when the elements are replaced: nowhere (a setting
// changed in place); to element of a new screen (the system's "screen
// changed"); or to element of a new page of the same screen, such as
// another tab or licence page (its "layout changed").
struct AccessibilityFocus {
  enum class Kind : std::uint8_t { Stay, Screen, Page };
  Kind kind = Kind::Stay;
  std::size_t element = 0;
};

#if defined(PINBALL_IOS)
// The system's Reduce Motion setting is on.
bool reduceMotionEnabled() noexcept;
// Another application's audio is playing: the game then starts without its
// tune.
bool otherAudioPlaying() noexcept;
// How many times an audio device has come or gone (headphones in or out, a
// speaker connected) since the first call, which starts the count at 0. A
// number that has moved is a reason to try opening the audio device again.
// Other route changes, such as the session's own when the device is
// opened, are not counted.
unsigned audioRouteChanges() noexcept;
// The window the game draws in: the accessibility elements belong to its
// view. Also sets up the haptics (Core Haptics' engine, started now and
// whenever the application becomes active again) and logs which path they
// take. Call once the window exists.
void attachWindow(SDL_Window* window) noexcept;
// Whether this device has haptics to give (a phone; not an iPad).
bool hapticsAvailable() noexcept;
// Plays one haptic: core/Feedback.cpp's pattern for the kind on Core
// Haptics, or the nearest UIKit feedback where the hardware has no Core
// Haptics. Does nothing where there are none. Main thread (another hands
// it there). The caller limits the rate (HapticGate).
void haptic(Haptic kind) noexcept;
// Asks the system to show its store review prompt, which it may or may not
// do (it limits how often). The game never draws one of its own.
void requestReview() noexcept;
// Replaces what VoiceOver finds on the game's view: these elements, or with
// none the view as SDL left it, and moves VoiceOver as focus says. UIKit's,
// so the main thread's alone: called from another, it changes nothing and
// returns false, and the caller tries again later.
[[nodiscard]] bool setAccessibilityElements(std::span<const AccessibleElement> elements,
                                            std::uint32_t eventType,
                                            AccessibilityFocus focus) noexcept;
#else
inline bool reduceMotionEnabled() noexcept { return false; }
inline bool otherAudioPlaying() noexcept { return false; }
inline unsigned audioRouteChanges() noexcept { return 0; }
inline void attachWindow(SDL_Window* /*window*/) noexcept {}
inline bool hapticsAvailable() noexcept { return false; }
inline void haptic(Haptic /*kind*/) noexcept {}
inline void requestReview() noexcept {}
[[nodiscard]] inline bool setAccessibilityElements(
    std::span<const AccessibleElement> /*elements*/, std::uint32_t /*eventType*/,
    AccessibilityFocus /*focus*/) noexcept {
  return true;
}
#endif

}  // namespace pinball::ios
