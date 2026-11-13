#include "core/TouchControls.hpp"

namespace pinball {

void TouchControls::handle(const TouchEvent& /*event*/, const screen::Layout& /*layout*/,
                           const Context& /*context*/) noexcept {}

void TouchControls::cancelAll() noexcept {}

InputState TouchControls::held() const noexcept { return {}; }

InputState TouchControls::takePressed() noexcept { return {}; }

bool TouchControls::takePauseRequest() noexcept { return false; }

TouchControls::View TouchControls::view(const Context& context) const noexcept {
  View view{};
  view.centreRole = context.gameOver             ? View::Centre::Play
                    : context.ballAwaitingLaunch ? View::Centre::Launch
                                                 : View::Centre::Bump;
  return view;
}

}  // namespace pinball
