// Touches: fingers on the on-screen controls, mapped by core/TouchControls
// to the same input the keyboard gives, and taps on the pause menu, the
// About screen and the initials prompt.
#include "platform/App.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/OverlayLayout.hpp"
#include "core/Version.hpp"

namespace pinball {

namespace {

// One number for a finger. Finger ids are only unique within the device
// that reports them (a touch screen, or the mouse standing in for one).
constexpr std::uint64_t fingerKey(SDL_TouchID device, SDL_FingerID finger) noexcept {
  return static_cast<std::uint64_t>(finger) +
         static_cast<std::uint64_t>(device) * 0x9E3779B97F4A7C15ULL;
}

}  // namespace

App::Screen App::screen() const noexcept {
  if (aboutOpen_) {
    return Screen::About;
  }
  if (entry_) {
    return Screen::Prompt;
  }
  return paused_ || autoPaused_ ? Screen::Menu : Screen::Play;
}

void App::syncScreen() {
  const Screen now = screen();
  if (now == lastScreen_) {
    return;
  }
  lastScreen_ = now;
  // A finger that was holding a flipper when the game stopped does not hold
  // it when the game goes on, and one that was on a button of one screen
  // does not press a button of the next.
  touch_.cancelAll();
  overlayTaps_.cancel();
  if (now != Screen::Menu) {
    confirmNewGame_ = false;
  }
  needsRedraw_ = true;
}

TouchControls::Context App::touchContext() const noexcept {
  const bool over = game_.gameOver();
  // Until core says so itself: a ball that is not yet in play is the one on
  // the plunger.
  return {.ballAwaitingLaunch = !over && !game_.rules().ballInPlay(), .gameOver = over};
}

void App::resumePlay() {
  paused_ = false;
  interrupted_ = false;
  confirmNewGame_ = false;
  // Nothing pressed during a pause carries into play.
  latched_ = InputState{};
}

void App::handleTouch(const SDL_Event& event) {
  if (layout_.idiom != screen::Idiom::Touch) {
    // The desktop has no on-screen controls.
    return;
  }
  TouchPhase phase = TouchPhase::Cancel;
  switch (event.type) {
    case SDL_EVENT_FINGER_DOWN:
      phase = TouchPhase::Down;
      break;
    case SDL_EVENT_FINGER_MOTION:
      phase = TouchPhase::Move;
      break;
    case SDL_EVENT_FINGER_UP:
      phase = TouchPhase::Up;
      break;
    default:
      break;
  }
  const std::uint64_t id = fingerKey(event.tfinger.touchID, event.tfinger.fingerID);
  // SDL gives a finger's place as a fraction of the window.
  const CanvasPoint at = canvasPoint(layout_, event.tfinger.x, event.tfinger.y);
  const Uint64 now = SDL_GetTicks();

  switch (screen()) {
    case Screen::Play:
      touch_.handle({.id = id, .phase = phase, .x = at.x, .y = at.y}, layout_, touchContext());
      if (touch_.takePauseRequest()) {
        paused_ = true;
        latched_ = InputState{};
      }
      break;
    case Screen::Menu:
      touchMenu(overlayTaps_.handle(phase, id, menuTarget(screen::menuLayout(layout_), at.x, at.y),
                                    now));
      break;
    case Screen::About:
      touchAbout(overlayTaps_.handle(
          phase, id, aboutTarget(screen::aboutLayout(layout_), at.x, at.y), now));
      break;
    case Screen::Prompt:
      // The prompt appears the moment the last ball drains, while thumbs
      // are still coming down on the flippers. A touch that begins in the
      // first moments is play, not an answer.
      if (phase == TouchPhase::Down && now - entryOpenedMs_ < kEntryGuardMs) {
        break;
      }
      touchInitials(overlayTaps_.handle(
          phase, id, initialsTarget(screen::initialsLayout(layout_), at.x, at.y), now));
      break;
  }
}

void App::touchMenu(OverlayTarget acted) {
  using Kind = OverlayTarget::Kind;
  switch (acted.kind) {
    case Kind::MenuResume:
    case Kind::Outside:
      resumePlay();
      break;
    case Kind::MenuNewGame:
      // A game in progress is not thrown away by one stray tap.
      if (!confirmNewGame_ && !game_.gameOver()) {
        confirmNewGame_ = true;
        break;
      }
      resumePlay();
      latched_.newGame = true;
      break;
    case Kind::MenuSound:
      confirmNewGame_ = false;
      mixer_.setMuted(!mixer_.muted());
      break;
    case Kind::MenuMusic:
      confirmNewGame_ = false;
      musicOn_ = !musicOn_;
      break;
    case Kind::MenuAbout:
      confirmNewGame_ = false;
      aboutOpen_ = true;
      aboutState_ = AboutState{};
      break;
    default:
      break;
  }
}

void App::touchAbout(OverlayTarget acted) {
  using Kind = OverlayTarget::Kind;
  switch (acted.kind) {
    case Kind::AboutClose:
    case Kind::Outside:
      aboutOpen_ = false;
      latched_ = InputState{};
      break;
    case Kind::AboutTab:
      if (acted.index <= static_cast<std::uint8_t>(AboutPage::Licences)) {
        aboutState_ = AboutState{.page = static_cast<AboutPage>(acted.index), .licencePage = 0};
      }
      break;
    case Kind::AboutPrivacy: {
      // The one thing the game ever hands to another application: the
      // address of the privacy policy, to the system's browser, on this tap.
      // A build constant, and only if it is a plain https address.
      const std::string_view url = kPrivacyUrl;
      if (aboutState_.page == AboutPage::About && url.starts_with("https://") &&
          !options_.frameLimit && !SDL_OpenURL(kPrivacyUrl)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not open the privacy policy: %s",
                    SDL_GetError());
      }
      break;
    }
    case Kind::AboutPrevious:
      if (aboutState_.page == AboutPage::Licences && aboutState_.licencePage > 0) {
        --aboutState_.licencePage;
      }
      break;
    case Kind::AboutNext:
      if (aboutState_.page == AboutPage::Licences &&
          aboutState_.licencePage + 1 < licencePageCount(layout_)) {
        ++aboutState_.licencePage;
      }
      break;
    default:
      break;
  }
}

void App::touchInitials(OverlayTarget acted) {
  using Kind = OverlayTarget::Kind;
  if (!entry_) {
    return;
  }
  const auto select = [this](std::size_t letter) {
    // The cursor only steps; each call moves it one place or not at all.
    for (std::size_t i = 0; i < Initials{}.size() && entry_->cursor() != letter; ++i) {
      if (entry_->cursor() > letter) {
        entry_->left();
      } else {
        entry_->right();
      }
    }
  };
  switch (acted.kind) {
    case Kind::InitialUp:
      select(acted.index);
      entry_->next();
      break;
    case Kind::InitialDown:
      select(acted.index);
      entry_->previous();
      break;
    case Kind::InitialSlot:
      select(acted.index);
      break;
    case Kind::InitialsOk:
      commitInitials();
      break;
    default:
      break;
  }
}

void App::repeatHeldArrow() {
  if (screen() != Screen::Prompt) {
    return;
  }
  const OverlayTarget arrow = overlayTaps_.repeat(SDL_GetTicks());
  if (arrow.kind != OverlayTarget::Kind::None) {
    touchInitials(arrow);
    needsRedraw_ = true;
  }
}

}  // namespace pinball
