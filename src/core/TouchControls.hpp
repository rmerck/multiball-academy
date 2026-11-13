#pragma once

#include <cstddef>
#include <cstdint>

#include "core/Input.hpp"
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

// Turns touches into the same InputState the keyboard produces. No SDL, so
// it is unit tested like the rest of core.
//
// Not written yet: every call is accepted and nothing is ever pressed.
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
  // changes.
  void cancelAll() noexcept;
  // Levels now.
  [[nodiscard]] InputState held() const noexcept;
  // Down since the last call. The same contract as the keyboard's latch: a
  // press shorter than a frame is still seen by one simulation step.
  [[nodiscard]] InputState takePressed() noexcept;
  [[nodiscard]] bool takePauseRequest() noexcept;
  [[nodiscard]] View view(const Context& context) const noexcept;

  static constexpr std::size_t kMaxTouches = 10;
};

}  // namespace pinball
