// The screens in front of the game: the title over attract mode, its menu
// and pages; the touch guide; the settings that persist; the haptics and
// the review prompt; and what VoiceOver finds on the menus.
#include <algorithm>
#include <array>
#include <deque>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/FrontLayout.hpp"
#include "core/HowToText.hpp"
#include "core/OverlayLayout.hpp"
#include "core/Version.hpp"
#include "platform/About.hpp"
#include "platform/App.hpp"
#include "platform/FrontScreens.hpp"
#include "platform/ios/IosSystem.hpp"

namespace pinball {

namespace {

// The sounds the menus make: the table's own cues.
constexpr SoundSet kMoveSound = soundBit(Sound::Rollover);
constexpr SoundSet kOpenSound = soundBit(Sound::Target);
constexpr SoundSet kCloseSound = soundBit(Sound::HoleOut);
constexpr SoundSet kPlaySound = soundBit(Sound::Launch);
constexpr SoundSet kAskSound = soundBit(Sound::TiltWarning);
constexpr SoundSet kResetSound = soundBit(Sound::Drain);
constexpr SoundSet kIntroSound = soundBit(Sound::SetComplete);

// The touch guide as VoiceOver reads it: every control it marks.
constexpr std::string_view kGuideSpoken =
    "TOUCH CONTROLS. LEFT HALF OF THE SCREEN: LEFT FLIPPER. RIGHT HALF: RIGHT FLIPPER. "
    "LAUNCH BUTTON: HOLD TO PULL THE PLUNGER, LET GO TO SHOOT. THE BUTTONS BESIDE IT NUDGE "
    "THE TABLE. PAUSE BUTTON AT THE TOP. ";

// What an accessibility element's code says: which group of controls, and
// the target in it.
enum class AccessGroup : std::int32_t { Front = 1, PauseMenu = 2, About = 3, Dismiss = 4 };
constexpr std::int32_t accessCode(AccessGroup group, std::uint8_t kind, std::uint8_t index) {
  return (static_cast<std::int32_t>(group) << 16) | (static_cast<std::int32_t>(kind) << 8) | index;
}

}  // namespace

FrontConfig App::frontConfig() const noexcept {
  const bool touch = layout_.idiom == screen::Idiom::Touch;
  return {.continueShown = hasSavedGame(), .hapticsShown = hapticsAvailable_, .guideShown = touch};
}

bool App::introDone() const noexcept {
  return !frontOpen_ || reduceMotion_ || introSeconds_ >= kTitleIntroSeconds;
}

void App::skipIntro() { introSeconds_ = std::max(introSeconds_, kTitleIntroSeconds); }

void App::openFront() {
  frontOpen_ = true;
  gameOverIdle_ = 0.0;
  front_.configure(frontConfig());
  front_.reset();
  introSeconds_ = 0.0;
  frontSeconds_ = 0.0;
  frontFinger_.reset();
  // A fresh attract game each time: it never carries anything of the
  // player's, and the player's game is never stepped behind it.
  attract_.emplace();
  pilot_ = AttractPilot{};
  attractTimestep_ = FixedTimestep{Game::kStepSeconds, 8};
  attractSteps_ = 0;
  touch_.cancelAll();
  overlayTaps_.cancel();
  latched_ = InputState{};
  if (!reduceMotion_) {
    mixer_.trigger(kIntroSound);
  }
  maybeRequestReview();
  redrawsDue_ = kRedrawsAfterEvent;
}

void App::startGame() {
  frontOpen_ = false;
  attract_.reset();
  frontFinger_.reset();
  aboutOpen_ = false;
  paused_ = false;
  interrupted_ = false;
  confirmNewGame_ = false;
  confirmMainMenu_ = false;
  // The guide to the touch controls, once, before the player's first game.
  guideOpen_ = layout_.idiom == screen::Idiom::Touch && !settings_.touchGuideSeen;
  latched_ = InputState{};
  // A new game whatever the one behind the title was doing; taken by the
  // first step after the guide.
  latched_.newGame = true;
  mixer_.trigger(kPlaySound);
  redrawsDue_ = kRedrawsAfterEvent;
}

bool App::gameOverStanding() const noexcept {
  return titleRun_ && !frontOpen_ && !guideOpen_ && game_.gameOver() && !entry_ && !aboutOpen_ &&
         !paused_ && !autoPaused_;
}

void App::continueSavedGame() {
  if (!resumeSavedGame()) {
    // It could not be resumed (and is gone): the title without it.
    front_.configure(frontConfig());
    front_.reset();
    mixer_.trigger(kAskSound);
    return;
  }
  // The saved game is on the table, paused as it was left.
  frontOpen_ = false;
  attract_.reset();
  frontFinger_.reset();
  guideOpen_ = false;
  confirmMainMenu_ = false;
  mixer_.trigger(kPlaySound);
  redrawsDue_ = kRedrawsAfterEvent;
}

void App::goToMainMenu() {
  paused_ = false;
  autoPaused_ = false;
  interrupted_ = false;
  confirmNewGame_ = false;
  confirmMainMenu_ = false;
  aboutOpen_ = false;
  openFront();
}

void App::dismissGuide() {
  if (!guideOpen_) {
    return;
  }
  guideOpen_ = false;
  if (!settings_.touchGuideSeen) {
    settings_.touchGuideSeen = true;
    saveSettings();
  }
  // The new game the title asked for is still latched for the first step.
  const bool wanted = latched_.newGame;
  latched_ = InputState{};
  latched_.newGame = wanted;
  redrawsDue_ = kRedrawsAfterEvent;
}

void App::stepAttract(double seconds) {
  // Still on the touch guide (the controls must be seen as they are), and
  // still for whoever asks for less motion.
  if (!attract_ || reduceMotion_ || front_.state().page == FrontPage::Guide || backgrounded_) {
    return;
  }
  const int steps = options_.attractStepsPerFrame
                        ? std::clamp(*options_.attractStepsPerFrame, 1, kMaxAttractStepsPerFrame)
                        : attractTimestep_.advance(std::clamp(seconds, 0.0, 0.25));
  for (int i = 0; i < steps; ++i) {
    const bool wasOver = attract_->gameOver();
    attract_->step(pilot_.next(*attract_));
    ++attractSteps_;
    // An attract game that ends is counted, and that is all: it has no
    // initials, no score table and no saved game (recordFinishedGame and
    // the saved game follow the player's game alone).
    if (attract_->gameOver() && !wasOver) {
      ++attractGames_;
    }
  }
  // Its sounds are not played: attract mode is silent but for the tune.
}

TableLights App::attractLights(const Game& attract) const {
  return attractChase(attract, reduceMotion_);
}

void App::frontAct(FrontAction action) {
  redrawsDue_ = kRedrawsAfterEvent;
  switch (action) {
    case FrontAction::None:
      break;
    case FrontAction::Moved:
      mixer_.trigger(kMoveSound);
      break;
    case FrontAction::Opened:
    case FrontAction::ShowGuide:
      mixer_.trigger(kOpenSound);
      break;
    case FrontAction::Closed:
      mixer_.trigger(kCloseSound);
      break;
    case FrontAction::Continue:
      continueSavedGame();
      break;
    case FrontAction::Play:
      startGame();
      break;
    case FrontAction::OpenAbout:
      mixer_.trigger(kOpenSound);
      aboutOpen_ = true;
      aboutPage_ = AboutState{};
      break;
    case FrontAction::ToggleSound:
      setSound(!settings_.sound);
      mixer_.trigger(kMoveSound);
      break;
    case FrontAction::VolumeDown:
    case FrontAction::VolumeUp: {
      const int step = action == FrontAction::VolumeUp ? 1 : -1;
      settings_.volume = static_cast<std::uint8_t>(
          std::clamp(static_cast<int>(settings_.volume) + step, 0,
                     static_cast<int>(Settings::kMaxVolume)));
      applySettings();
      saveSettings();
      // At the new level, to hear it.
      mixer_.trigger(kMoveSound);
      break;
    }
    case FrontAction::ToggleMusic:
      setMusic(!settings_.music);
      mixer_.trigger(kMoveSound);
      break;
    case FrontAction::ToggleHaptics:
      settings_.haptics = !settings_.haptics;
      saveSettings();
      mixer_.trigger(kMoveSound);
      break;
    case FrontAction::AskReset:
      mixer_.trigger(kAskSound);
      break;
    case FrontAction::ResetScores:
      highScores_ = HighScores{};
      newHighScore_ = false;
      if (keepFiles_) {
        (void)scoreStore_.save(highScores_);
      }
      mixer_.trigger(kResetSound);
      break;
  }
}

void App::handleFrontKey(const SDL_KeyboardEvent& key) {
  // Keys that are not choices work as they do in the game.
  switch (key.key) {
    case SDLK_F1:
      aboutOpen_ = true;
      aboutPage_ = AboutState{};
      return;
    case SDLK_F2:
      startGame();
      return;
    case SDLK_F4:
      toggleFullscreen();
      return;
    default:
      break;
  }
  if (key.scancode == SDL_SCANCODE_M) {
    setSound(!settings_.sound);
    return;
  }
  if (key.scancode == SDL_SCANCODE_N) {
    setMusic(!settings_.music);
    return;
  }
  // Any other key while the title builds itself finishes it, and does
  // nothing more.
  if (!introDone()) {
    skipIntro();
    return;
  }
  std::optional<FrontCommand> command;
  switch (key.key) {
    case SDLK_UP:
      command = FrontCommand::Up;
      break;
    case SDLK_DOWN:
      command = FrontCommand::Down;
      break;
    case SDLK_LEFT:
      command = FrontCommand::Left;
      break;
    case SDLK_RIGHT:
      command = FrontCommand::Right;
      break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
      command = FrontCommand::Select;
      break;
    case SDLK_ESCAPE:
    case SDLK_BACKSPACE:
      command = FrontCommand::Back;
      break;
    default:
      break;
  }
  if (command) {
    frontAct(front_.command(*command));
  }
}

void App::touchFront(TouchPhase phase, std::uint64_t id, float x, float y) {
  if (phase == TouchPhase::Down && !frontFinger_) {
    frontFinger_ = id;
    if (!introDone()) {
      // A touch while the title builds itself finishes it, and is spent.
      skipIntro();
      frontDown_ = FrontTarget{};
      frontOnTarget_ = false;
      return;
    }
    frontDown_ = screen::frontTarget(layout_, front_.config(), front_.state(), x, y);
    frontOnTarget_ = frontDown_.kind != FrontTarget::Kind::None;
    return;
  }
  if (!frontFinger_ || *frontFinger_ != id) {
    return;
  }
  const FrontTarget under = screen::frontTarget(layout_, front_.config(), front_.state(), x, y);
  switch (phase) {
    case TouchPhase::Move:
      frontOnTarget_ = frontDown_.kind != FrontTarget::Kind::None && under == frontDown_;
      break;
    case TouchPhase::Up: {
      frontFinger_.reset();
      const FrontTarget down = frontDown_;
      frontOnTarget_ = false;
      if (down.kind != FrontTarget::Kind::None && under == down) {
        frontAct(front_.tap(down));
      }
      break;
    }
    case TouchPhase::Down:
    case TouchPhase::Cancel:
      frontFinger_.reset();
      frontOnTarget_ = false;
      break;
  }
}

void App::applySettings() {
  mixer_.setMuted(!settings_.sound);
  musicOn_ = settings_.music;
  if (audio_) {
    audio_->setGain(settings_.gain());
  }
}

void App::saveSettings() {
  if (keepFiles_) {
    (void)settingsStore_.save(settings_);
  }
}

void App::setSound(bool on) {
  settings_.sound = on;
  mixer_.setMuted(!on);
  saveSettings();
}

void App::setMusic(bool on) {
  settings_.music = on;
  musicOn_ = on;
  saveSettings();
}

void App::playHaptics(SoundSet sounds) {
  if (!hapticsAvailable_ || !settings_.haptics || sounds == 0 || options_.frameLimit) {
    return;
  }
  if (const std::optional<Haptic> kind = hapticGate_.take(sounds, SDL_GetTicks())) {
    ios::haptic(*kind);
    ++hapticsPlayed_;
  }
}

void App::maybeRequestReview() {
  // Only from the title, only after a game of this run has ended and with
  // no game going on behind it, and only in a run that keeps the player's
  // files (it is they that remember the version asked).
  if (!keepFiles_ || !game_.gameOver() || !reviewDue(settings_, kGameVersion, gameEndedThisRun_)) {
    return;
  }
  settings_.reviewAskedVersion = kGameVersion;
  saveSettings();
  ++reviewRequests_;
  ios::requestReview();
}

void App::refreshAccessibility() {
  if (layout_.idiom != screen::Idiom::Touch) {
    return;
  }
  // What the elements depend on, folded into one number.
  const Screen now = screen();
  const FrontState& state = front_.state();
  std::uint64_t key = static_cast<std::uint64_t>(now);
  const auto fold = [&key](std::uint64_t value) { key = key * 1099511628211ULL ^ value; };
  fold(static_cast<std::uint64_t>(state.page));
  fold(state.howToPage);
  fold(state.confirmReset ? 1U : 0U);
  fold(introDone() ? 1U : 0U);
  fold(confirmNewGame_ ? 1U : 0U);
  fold(confirmMainMenu_ ? 1U : 0U);
  fold(interrupted_ ? 1U : 0U);
  fold(static_cast<std::uint64_t>(aboutPage_.page));
  fold(mixer_.muted() ? 1U : 0U);
  fold(musicOn_ ? 1U : 0U);
  fold(settings_.volume);
  fold(settings_.haptics ? 1U : 0U);
  fold(static_cast<std::uint64_t>(layout_.canvasWidth * 4.0F));
  fold(static_cast<std::uint64_t>(layout_.canvasHeight * 4.0F));
  if (key == accessibilityKey_) {
    return;
  }
  // A new screen or page, not a setting changed on the same one: VoiceOver
  // moves to its first element.
  const std::uint64_t pageKey = (static_cast<std::uint64_t>(now) << 32) |
                             (static_cast<std::uint64_t>(state.page) << 16) | state.howToPage;
  const bool screenChanged = pageKey != accessibilityPage_;
  accessibilityPage_ = pageKey;
  accessibilityKey_ = key;

  // The labels live here for the call. A deque: adding one never moves the
  // others, which the elements point into.
  std::deque<std::string> labels;
  std::vector<ios::AccessibleElement> elements;
  const float ppu = layout_.pointsPerUnit;
  const auto add = [&](std::string label, const screen::Rect& r, std::int32_t code,
                       bool header) {
    if (r.w <= 0.0F || r.h <= 0.0F) {
      return;
    }
    labels.push_back(std::move(label));
    elements.push_back({labels.back(), {r.x * ppu, r.y * ppu, r.w * ppu, r.h * ppu}, code, header});
  };
  const auto front = [](FrontTarget::Kind kind, std::uint8_t index = 0) {
    return accessCode(AccessGroup::Front, static_cast<std::uint8_t>(kind), index);
  };
  const auto onOff = [](std::string_view name, bool on) {
    return std::string(name) + (on ? ": ON" : ": OFF");
  };
  using Kind = FrontTarget::Kind;
  const FrontConfig config = front_.config();
  switch (now) {
    case Screen::Front:
      if (!introDone()) {
        add("MULTIBALL ACADEMY. TAP TO SKIP.", screen::compositionArea(layout_),
            front(Kind::Anywhere), false);
        break;
      }
      switch (state.page) {
        case FrontPage::Title: {
          const screen::TitleLayout title = screen::titleLayout(layout_, config);
          add(kGameTitle, title.logo, 0, true);
          for (std::size_t i = 0; i < kTitleItemCount; ++i) {
            const auto item = static_cast<TitleItem>(i);
            if (front_.shown(item)) {
              add(std::string(titleItemLabel(item)), title.items[i],
                  front(Kind::Item, static_cast<std::uint8_t>(i)), false);
            }
          }
          break;
        }
        case FrontPage::Scores: {
          const screen::PageLayout page = screen::pageLayout(layout_, FrontPage::Scores);
          add("HIGH SCORES", page.heading, 0, true);
          std::string list;
          for (std::size_t i = 0; i < highScores_.size(); ++i) {
            const HighScore entry = highScores_.at(i);
            list += std::to_string(i + 1) + ". " +
                    std::string(entry.initials.data(), entry.initials.size()) + ", " +
                    std::to_string(entry.score) + ". ";
          }
          add(list.empty() ? "NO GAMES FINISHED YET" : list, page.body, 0, true);
          add("BACK", page.back, front(Kind::Back), false);
          break;
        }
        case FrontPage::HowTo: {
          const bool guide = config.guideShown && state.howToPage == 0;
          const screen::PageLayout page = screen::pageLayout(layout_, FrontPage::HowTo, guide);
          const howto::Page& text = howto::kPages[std::min(state.howToPage, kHowToPageCount - 1)];
          add("HOW TO PLAY. " + std::string(text.title), page.heading, 0, true);
          add(std::string(howto::text(text, true)), page.body, 0, true);
          add("PREVIOUS PAGE", page.previous, front(Kind::Previous), false);
          add("NEXT PAGE", page.next, front(Kind::Next), false);
          if (guide) {
            add("SHOW TOUCH GUIDE", page.guide, front(Kind::GuideButton), false);
          }
          add("BACK", page.back, front(Kind::Back), false);
          break;
        }
        case FrontPage::Settings: {
          const screen::SettingsLayout s = screen::settingsLayout(layout_, config);
          add("SETTINGS", s.page.heading, 0, true);
          const auto row = [&](SettingsRow r) { return s.rows[static_cast<std::size_t>(r)]; };
          const auto rowCode = [&](SettingsRow r) {
            return front(Kind::Row, static_cast<std::uint8_t>(r));
          };
          add(onOff("SOUND", settings_.sound), row(SettingsRow::Sound), rowCode(SettingsRow::Sound),
              false);
          add("VOLUME DOWN. VOLUME " + std::to_string(settings_.volume), s.volumeDown,
              front(Kind::VolumeDown), false);
          add("VOLUME UP. VOLUME " + std::to_string(settings_.volume), s.volumeUp,
              front(Kind::VolumeUp), false);
          add(onOff("MUSIC", settings_.music), row(SettingsRow::Music), rowCode(SettingsRow::Music),
              false);
          if (config.hapticsShown) {
            add(onOff("HAPTICS", settings_.haptics), row(SettingsRow::Haptics),
                rowCode(SettingsRow::Haptics), false);
          }
          add(state.confirmReset ? "TAP AGAIN TO RESET HIGH SCORES" : "RESET HIGH SCORES",
              row(SettingsRow::Reset), rowCode(SettingsRow::Reset), false);
          add("BACK", s.page.back, front(Kind::Back), false);
          break;
        }
        case FrontPage::Guide:
          add(std::string(kGuideSpoken) + "TAP TO GO BACK.",
              {0.0F, 0.0F, layout_.canvasWidth, layout_.canvasHeight}, front(Kind::Anywhere),
              false);
          break;
      }
      break;
    case Screen::Guide:
      add(std::string(kGuideSpoken) + "TAP TO PLAY.",
          {0.0F, 0.0F, layout_.canvasWidth, layout_.canvasHeight},
          accessCode(AccessGroup::Dismiss, 0, 0), false);
      break;
    case Screen::Menu: {
      const auto menuCode = [](OverlayTarget::Kind kind) {
        return accessCode(AccessGroup::PauseMenu, static_cast<std::uint8_t>(kind), 0);
      };
      if (interrupted_) {
        add("PAUSED. TAP TO RESUME", {0.0F, 0.0F, layout_.canvasWidth, layout_.canvasHeight},
            menuCode(OverlayTarget::Kind::Outside), false);
        break;
      }
      const screen::MenuLayout menu = screen::menuLayout(layout_);
      add("PAUSED", {menu.box.x, menu.box.y, menu.box.w, menu.resume.y - menu.box.y}, 0, true);
      add("RESUME", menu.resume, menuCode(OverlayTarget::Kind::MenuResume), false);
      add(confirmNewGame_ ? "TAP AGAIN TO CONFIRM NEW GAME" : "NEW GAME", menu.newGame,
          menuCode(OverlayTarget::Kind::MenuNewGame), false);
      add(onOff("SOUND", !mixer_.muted()), menu.sound, menuCode(OverlayTarget::Kind::MenuSound),
          false);
      add(onOff("MUSIC", musicOn_), menu.music, menuCode(OverlayTarget::Kind::MenuMusic), false);
      add("SCORES AND ABOUT", menu.about, menuCode(OverlayTarget::Kind::MenuAbout), false);
      add(confirmMainMenu_ ? "TAP AGAIN TO LEAVE THE GAME" : "MAIN MENU", menu.mainMenu,
          menuCode(OverlayTarget::Kind::MenuMainMenu), false);
      break;
    }
    case Screen::About: {
      const screen::AboutLayout about = screen::aboutLayout(layout_);
      constexpr std::array<std::string_view, 3> kTabs{"SCORES", "ABOUT", "LICENCES"};
      for (std::size_t i = 0; i < kTabs.size(); ++i) {
        add(std::string(kTabs[i]), about.tabs[i],
            accessCode(AccessGroup::About, static_cast<std::uint8_t>(OverlayTarget::Kind::AboutTab),
                       static_cast<std::uint8_t>(i)),
            false);
      }
      add("CLOSE", about.close,
          accessCode(AccessGroup::About, static_cast<std::uint8_t>(OverlayTarget::Kind::AboutClose),
                     0),
          false);
      break;
    }
    case Screen::Play:
    case Screen::Prompt:
      // The game itself is played by touch, not read.
      break;
  }
  ios::setAccessibilityElements(elements, accessibilityEvent_, screenChanged);
}

void App::handleAccessibilityAction(std::int32_t code) {
  const auto group = static_cast<AccessGroup>(code >> 16);
  const auto kind = static_cast<std::uint8_t>((code >> 8) & 0xFF);
  const auto index = static_cast<std::uint8_t>(code & 0xFF);
  switch (group) {
    case AccessGroup::Front:
      if (screen() != Screen::Front) {
        break;
      }
      if (!introDone()) {
        skipIntro();
        break;
      }
      frontAct(front_.tap({static_cast<FrontTarget::Kind>(kind), index}));
      break;
    case AccessGroup::PauseMenu:
      if (screen() == Screen::Menu) {
        touchMenu({static_cast<OverlayTarget::Kind>(kind), index});
      }
      break;
    case AccessGroup::About:
      if (screen() == Screen::About) {
        touchAbout({static_cast<OverlayTarget::Kind>(kind), index});
      }
      break;
    case AccessGroup::Dismiss:
      if (screen() == Screen::Guide) {
        dismissGuide();
      }
      break;
  }
  syncScreen();
}

}  // namespace pinball
