#pragma once

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
// The window the game draws in: the haptics and the accessibility elements
// belong to its view. Call once it exists.
void attachWindow(SDL_Window* window) noexcept;
// Whether this device has haptics to give (a phone; not an iPad).
bool hapticsAvailable() noexcept;
// Plays one haptic. Does nothing where there are none. The caller limits
// the rate (HapticGate).
void haptic(Haptic kind) noexcept;
// Asks the system to show its store review prompt, which it may or may not
// do (it limits how often). The game never draws one of its own.
void requestReview() noexcept;
// Replaces what VoiceOver finds on the game's view: these elements, or with
// none the view as SDL left it. screenChanged: a new screen is up, and
// VoiceOver moves to its first element.
void setAccessibilityElements(std::span<const AccessibleElement> elements, std::uint32_t eventType,
                              bool screenChanged) noexcept;
#else
inline bool reduceMotionEnabled() noexcept { return false; }
inline bool otherAudioPlaying() noexcept { return false; }
inline unsigned audioRouteChanges() noexcept { return 0; }
inline void attachWindow(SDL_Window* /*window*/) noexcept {}
inline bool hapticsAvailable() noexcept { return false; }
inline void haptic(Haptic /*kind*/) noexcept {}
inline void requestReview() noexcept {}
inline void setAccessibilityElements(std::span<const AccessibleElement> /*elements*/,
                                     std::uint32_t /*eventType*/,
                                     bool /*screenChanged*/) noexcept {}
#endif

}  // namespace pinball::ios
