#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/Input.hpp"
#include "core/OverlayLayout.hpp"
#include "core/ScreenLayout.hpp"

namespace pinball {

enum class TouchPhase : std::uint8_t { Down, Move, Up, Cancel };

// One change of one finger. x and y are in canvas units.
struct TouchEvent {
  std::uint64_t id;
  TouchPhase phase;
  float x;
  float y;
};

// A point on the canvas.
struct CanvasPoint {
  float x;
  float y;
};

// Where a touch is on the canvas, from its place in the window as a
// fraction of the window's width and height (what SDL reports for a
// finger). A touch layout's canvas covers the whole window, so this is all
// the mapping there is. A position that is not a number lands outside every
// control.
[[nodiscard]] CanvasPoint canvasPoint(const screen::Layout& layout, float fractionX,
                                      float fractionY) noexcept;

// iPadOS puts the handle that resizes an app's window in the window's
// bottom corners, and a drag that starts there is the system's: it must
// not press a control or tap a menu as well. A touch that goes down within
// this many points of either bottom corner is left to the system.
inline constexpr float kSystemCornerPoints = 32.0F;
[[nodiscard]] bool inSystemCorner(const screen::Layout& layout, CanvasPoint point) noexcept;

// Turns touches on the playing controls into the same InputState the
// keyboard produces. No SDL, so it is unit tested like the rest of core.
//
// Each finger is given its control when it goes down and keeps it until it
// lifts, wherever it slides: a thumb that drifts across the centre line
// still holds its own flipper. A finger that goes down on a button never
// also works the flipper zone the button lies in. Fingers beyond
// kMaxTouches, and fingers that go down on no control, are ignored until
// they lift.
class TouchControls {
 public:
  // What the centre button and the labels depend on.
  struct Context {
    bool ballAwaitingLaunch;
    bool gameOver;
  };
  // What to draw: which controls are held, and what the centre button does.
  struct View {
    bool left;
    bool right;
    bool centre;
    bool nudgeLeft;
    bool nudgeRight;
    bool pause;
    enum class Centre : std::uint8_t { Launch, Bump, Play } centreRole;
  };

  void handle(const TouchEvent& event, const screen::Layout& layout,
              const Context& context) noexcept;
  // Lets go of everything: on a halt, in the background, when the layout
  // changes. The fingers that were down are forgotten, so they do nothing
  // more until they lift and come down again, and no pause is requested.
  void cancelAll() noexcept;
  // Levels now.
  [[nodiscard]] InputState held() const noexcept;
  // Down since the last call. The same contract as the keyboard's latch: a
  // press shorter than a frame is still seen by one simulation step.
  [[nodiscard]] InputState takePressed() noexcept;
  // True once after a finger that went down on the pause button has lifted
  // inside it. A brush across the button does not pause.
  [[nodiscard]] bool takePauseRequest() noexcept;
  [[nodiscard]] View view(const Context& context) const noexcept;

  static constexpr std::size_t kMaxTouches = 10;

 private:
  enum class Role : std::uint8_t {
    Free,  // The slot holds no finger.
    LeftFlipper,
    RightFlipper,
    Launch,
    Bump,
    Play,
    NudgeLeft,
    NudgeRight,
    Pause,
  };
  struct Slot {
    std::uint64_t id = 0;
    Role role = Role::Free;
  };

  [[nodiscard]] Slot* find(std::uint64_t id) noexcept;
  [[nodiscard]] bool any(Role role) const noexcept;
  void press(Role role) noexcept;

  std::array<Slot, kMaxTouches> slots_{};
  InputState pressed_{};
  bool pauseRequested_ = false;
};

// One thing a finger can land on in an overlay (the pause menu, the About
// screen, the initials prompt).
struct OverlayTarget {
  enum class Kind : std::uint8_t {
    None,     // Inside the overlay's box, on nothing.
    Outside,  // Outside the box.
    MenuResume,
    MenuNewGame,
    MenuSound,
    MenuMusic,
    MenuAbout,
    AboutClose,
    AboutTab,  // index: the page, 0 to 2.
    AboutPrivacy,
    AboutPrevious,
    AboutNext,
    InitialUp,  // index: the letter, 0 to 2. Also for InitialSlot, InitialDown.
    InitialSlot,
    InitialDown,
    InitialsOk,
  };
  Kind kind = Kind::None;
  std::uint8_t index = 0;

  [[nodiscard]] constexpr bool operator==(const OverlayTarget&) const noexcept = default;
};

// What lies under a point of each overlay.
[[nodiscard]] OverlayTarget menuTarget(const screen::MenuLayout& menu, float x, float y) noexcept;
[[nodiscard]] OverlayTarget aboutTarget(const screen::AboutLayout& about, float x,
                                        float y) noexcept;
[[nodiscard]] OverlayTarget initialsTarget(const screen::InitialsLayout& initials, float x,
                                           float y) noexcept;

// Taps on an overlay. One finger at a time: the first one down owns the
// overlay until it lifts, and the others are ignored. A button acts when
// the finger lifts on the target it went down on, so a finger that slides
// off has changed its mind. The letter arrows of the initials prompt act at
// once instead, and repeat while they are held.
class OverlayTaps {
 public:
  // A held arrow repeats after this long, then this often.
  static constexpr std::uint64_t kRepeatDelayMs = 400;
  static constexpr std::uint64_t kRepeatPeriodMs = 125;

  // under is what lies beneath the finger now. Returns what to act on, or
  // None.
  [[nodiscard]] OverlayTarget handle(TouchPhase phase, std::uint64_t id, OverlayTarget under,
                                     std::uint64_t nowMs) noexcept;
  // The held arrow, each time another repeat of it has come due; else None.
  // At most one per call: after a long gap the repeats that were missed are
  // dropped, not played back in a burst.
  [[nodiscard]] OverlayTarget repeat(std::uint64_t nowMs) noexcept;
  // Forgets the finger. Nothing acts.
  void cancel() noexcept;
  // The target under the owning finger while it is still on the one it went
  // down on, for drawing it pressed; else None.
  [[nodiscard]] OverlayTarget pressed() const noexcept;

 private:
  bool active_ = false;
  bool onTarget_ = false;
  std::uint64_t id_ = 0;
  OverlayTarget target_{};
  // When the held arrow last acted, and how long until it does again.
  std::uint64_t lastActedMs_ = 0;
  std::uint64_t waitMs_ = kRepeatDelayMs;
};

}  // namespace pinball
