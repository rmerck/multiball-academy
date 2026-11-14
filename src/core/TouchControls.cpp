#include "core/TouchControls.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace pinball {

using screen::contains;

CanvasPoint canvasPoint(const screen::Layout& layout, float fractionX, float fractionY) noexcept {
  if (!std::isfinite(fractionX) || !std::isfinite(fractionY)) {
    // Left of and above everything.
    return {-1.0F, -1.0F};
  }
  return {fractionX * layout.canvasWidth, fractionY * layout.canvasHeight};
}

bool inSystemCorner(const screen::Layout& layout, CanvasPoint point) noexcept {
  if (!(layout.pointsPerUnit > 0.0F)) {
    return false;
  }
  const float reach = kSystemCornerPoints / layout.pointsPerUnit;
  const bool low = point.y >= layout.canvasHeight - reach && point.y <= layout.canvasHeight;
  const bool left = point.x >= 0.0F && point.x <= reach;
  const bool right = point.x >= layout.canvasWidth - reach && point.x <= layout.canvasWidth;
  return low && (left || right);
}

TouchControls::Slot* TouchControls::find(std::uint64_t id) noexcept {
  const auto found = std::ranges::find_if(
      slots_, [id](const Slot& slot) noexcept { return slot.role != Role::Free && slot.id == id; });
  return found == slots_.end() ? nullptr : &*found;
}

bool TouchControls::any(Role role) const noexcept {
  return std::ranges::any_of(slots_,
                             [role](const Slot& slot) noexcept { return slot.role == role; });
}

void TouchControls::press(Role role) noexcept {
  switch (role) {
    case Role::LeftFlipper:
      pressed_.leftFlipper = true;
      break;
    case Role::RightFlipper:
      pressed_.rightFlipper = true;
      break;
    case Role::Launch:
      pressed_.plunger = true;
      break;
    case Role::Bump:
      pressed_.nudgeUp = true;
      break;
    case Role::Play:
      pressed_.newGame = true;
      break;
    case Role::NudgeLeft:
      pressed_.nudgeLeft = true;
      break;
    case Role::NudgeRight:
      pressed_.nudgeRight = true;
      break;
    case Role::Pause:
    case Role::Free:
      break;
  }
}

void TouchControls::handle(const TouchEvent& event, const screen::Layout& layout,
                           const Context& context) noexcept {
  Slot* slot = find(event.id);
  switch (event.phase) {
    case TouchPhase::Down: {
      if (slot != nullptr) {
        // It never lifted, as far as we were told. Start again.
        slot->role = Role::Free;
      }
      // The buttons first: they lie inside the flipper zones.
      Role role = Role::Free;
      if (contains(layout.pauseButton, event.x, event.y)) {
        role = Role::Pause;
      } else if (contains(layout.nudgeLeftButton, event.x, event.y)) {
        role = Role::NudgeLeft;
      } else if (contains(layout.nudgeRightButton, event.x, event.y)) {
        role = Role::NudgeRight;
      } else if (contains(layout.centreButton, event.x, event.y)) {
        // Settled now, for as long as the finger stays down.
        role = context.gameOver             ? Role::Play
               : context.ballAwaitingLaunch ? Role::Launch
                                            : Role::Bump;
      } else if (contains(layout.flipperLeftZone, event.x, event.y)) {
        role = Role::LeftFlipper;
      } else if (contains(layout.flipperRightZone, event.x, event.y)) {
        role = Role::RightFlipper;
      }
      if (role == Role::Free) {
        return;
      }
      const auto free = std::ranges::find(slots_, Role::Free, &Slot::role);
      if (free == slots_.end()) {
        // More fingers than are followed. This one does nothing.
        return;
      }
      *free = Slot{event.id, role};
      press(role);
      break;
    }
    case TouchPhase::Move:
      // A finger keeps its control wherever it goes.
      break;
    case TouchPhase::Up:
      if (slot != nullptr) {
        if (slot->role == Role::Pause && contains(layout.pauseButton, event.x, event.y)) {
          pauseRequested_ = true;
        }
        slot->role = Role::Free;
      }
      break;
    case TouchPhase::Cancel:
      if (slot != nullptr) {
        slot->role = Role::Free;
      }
      break;
  }
}

void TouchControls::cancelAll() noexcept {
  slots_ = {};
  pressed_ = InputState{};
  pauseRequested_ = false;
}

InputState TouchControls::held() const noexcept {
  InputState state;
  state.leftFlipper = any(Role::LeftFlipper);
  state.rightFlipper = any(Role::RightFlipper);
  state.plunger = any(Role::Launch);
  state.nudgeLeft = any(Role::NudgeLeft);
  state.nudgeRight = any(Role::NudgeRight);
  state.nudgeUp = any(Role::Bump);
  state.newGame = any(Role::Play);
  return state;
}

InputState TouchControls::takePressed() noexcept {
  const InputState pressed = pressed_;
  pressed_ = InputState{};
  return pressed;
}

bool TouchControls::takePauseRequest() noexcept {
  const bool requested = pauseRequested_;
  pauseRequested_ = false;
  return requested;
}

TouchControls::View TouchControls::view(const Context& context) const noexcept {
  View view{};
  view.left = any(Role::LeftFlipper);
  view.right = any(Role::RightFlipper);
  view.nudgeLeft = any(Role::NudgeLeft);
  view.nudgeRight = any(Role::NudgeRight);
  view.pause = any(Role::Pause);
  // While a finger is on the centre button it shows what that finger is
  // doing; otherwise what the next touch would do.
  if (any(Role::Launch)) {
    view.centre = true;
    view.centreRole = View::Centre::Launch;
  } else if (any(Role::Bump)) {
    view.centre = true;
    view.centreRole = View::Centre::Bump;
  } else if (any(Role::Play)) {
    view.centre = true;
    view.centreRole = View::Centre::Play;
  } else {
    view.centreRole = context.gameOver             ? View::Centre::Play
                      : context.ballAwaitingLaunch ? View::Centre::Launch
                                                   : View::Centre::Bump;
  }
  return view;
}

OverlayTarget menuTarget(const screen::MenuLayout& menu, float x, float y) noexcept {
  using Kind = OverlayTarget::Kind;
  if (!contains(menu.box, x, y)) {
    return {Kind::Outside, 0};
  }
  if (contains(menu.resume, x, y)) {
    return {Kind::MenuResume, 0};
  }
  if (contains(menu.newGame, x, y)) {
    return {Kind::MenuNewGame, 0};
  }
  if (contains(menu.sound, x, y)) {
    return {Kind::MenuSound, 0};
  }
  if (contains(menu.music, x, y)) {
    return {Kind::MenuMusic, 0};
  }
  if (contains(menu.about, x, y)) {
    return {Kind::MenuAbout, 0};
  }
  if (contains(menu.mainMenu, x, y)) {
    return {Kind::MenuMainMenu, 0};
  }
  return {Kind::None, 0};
}

OverlayTarget aboutTarget(const screen::AboutLayout& about, float x, float y) noexcept {
  using Kind = OverlayTarget::Kind;
  if (!contains(about.box, x, y)) {
    return {Kind::Outside, 0};
  }
  if (contains(about.close, x, y)) {
    return {Kind::AboutClose, 0};
  }
  for (std::size_t i = 0; i < about.tabs.size(); ++i) {
    if (contains(about.tabs[i], x, y)) {
      return {Kind::AboutTab, static_cast<std::uint8_t>(i)};
    }
  }
  if (contains(about.privacyLink, x, y)) {
    return {Kind::AboutPrivacy, 0};
  }
  if (contains(about.previous, x, y)) {
    return {Kind::AboutPrevious, 0};
  }
  if (contains(about.next, x, y)) {
    return {Kind::AboutNext, 0};
  }
  return {Kind::None, 0};
}

OverlayTarget initialsTarget(const screen::InitialsLayout& initials, float x, float y) noexcept {
  using Kind = OverlayTarget::Kind;
  if (!contains(initials.box, x, y)) {
    return {Kind::Outside, 0};
  }
  if (contains(initials.ok, x, y)) {
    return {Kind::InitialsOk, 0};
  }
  for (std::size_t i = 0; i < initials.slot.size(); ++i) {
    const auto index = static_cast<std::uint8_t>(i);
    if (contains(initials.up[i], x, y)) {
      return {Kind::InitialUp, index};
    }
    if (contains(initials.down[i], x, y)) {
      return {Kind::InitialDown, index};
    }
    if (contains(initials.slot[i], x, y)) {
      return {Kind::InitialSlot, index};
    }
  }
  return {Kind::None, 0};
}

namespace {

constexpr bool repeats(OverlayTarget target) noexcept {
  return target.kind == OverlayTarget::Kind::InitialUp ||
         target.kind == OverlayTarget::Kind::InitialDown;
}

}  // namespace

OverlayTarget OverlayTaps::handle(TouchPhase phase, std::uint64_t id, OverlayTarget under,
                                  std::uint64_t nowMs) noexcept {
  constexpr OverlayTarget kNothing{};
  if (phase == TouchPhase::Down) {
    if (active_ && id != id_) {
      return kNothing;
    }
    active_ = true;
    onTarget_ = true;
    id_ = id;
    target_ = under;
    lastActedMs_ = nowMs;
    waitMs_ = kRepeatDelayMs;
    return repeats(under) ? under : kNothing;
  }
  if (!active_ || id != id_) {
    return kNothing;
  }
  switch (phase) {
    case TouchPhase::Move: {
      const bool on = under == target_;
      if (on && !onTarget_) {
        // Back on an arrow: it waits the full delay again.
        lastActedMs_ = nowMs;
        waitMs_ = kRepeatDelayMs;
      }
      onTarget_ = on;
      return kNothing;
    }
    case TouchPhase::Up: {
      active_ = false;
      const bool tapped =
          under == target_ && target_.kind != OverlayTarget::Kind::None && !repeats(target_);
      return tapped ? target_ : kNothing;
    }
    case TouchPhase::Cancel:
    case TouchPhase::Down:
      break;
  }
  active_ = false;
  return kNothing;
}

OverlayTarget OverlayTaps::repeat(std::uint64_t nowMs) noexcept {
  if (!active_ || !onTarget_ || !repeats(target_)) {
    return {};
  }
  // Measured from the last time it acted, so a gap gives one repeat.
  if (nowMs < lastActedMs_ || nowMs - lastActedMs_ < waitMs_) {
    return {};
  }
  lastActedMs_ = nowMs;
  waitMs_ = kRepeatPeriodMs;
  return target_;
}

void OverlayTaps::cancel() noexcept { active_ = false; }

OverlayTarget OverlayTaps::pressed() const noexcept {
  const bool shown = active_ && onTarget_ && target_.kind != OverlayTarget::Kind::None &&
                     target_.kind != OverlayTarget::Kind::Outside;
  return shown ? target_ : OverlayTarget{};
}

}  // namespace pinball
