#include "core/FrontMenu.hpp"

#include <utility>

namespace pinball {

FrontMenu::FrontMenu(FrontConfig config) noexcept : config_(config) { reset(); }

void FrontMenu::configure(FrontConfig config) noexcept {
  config_ = config;
  if (!shown(state_.titleFocus)) {
    state_.titleFocus = TitleItem::Play;
  }
  if (!shown(state_.settingsFocus)) {
    state_.settingsFocus = SettingsRow::Sound;
  }
  if (state_.page == FrontPage::Guide && !config_.guideShown) {
    state_.page = state_.guideReturn;
  }
}

void FrontMenu::reset() noexcept {
  state_ = FrontState{};
  state_.titleFocus = config_.continueShown ? TitleItem::Continue : TitleItem::Play;
}

bool FrontMenu::shown(TitleItem item) const noexcept {
  return item != TitleItem::Continue || config_.continueShown;
}

bool FrontMenu::shown(SettingsRow row) const noexcept {
  return row != SettingsRow::Haptics || config_.hapticsShown;
}

void FrontMenu::moveTitle(int step) noexcept {
  auto index = static_cast<int>(std::to_underlying(state_.titleFocus));
  for (std::size_t tries = 0; tries < kTitleItemCount; ++tries) {
    index = (index + step + static_cast<int>(kTitleItemCount)) % static_cast<int>(kTitleItemCount);
    const auto item = static_cast<TitleItem>(index);
    if (shown(item)) {
      state_.titleFocus = item;
      return;
    }
  }
}

void FrontMenu::moveSettings(int step) noexcept {
  auto index = static_cast<int>(std::to_underlying(state_.settingsFocus));
  for (std::size_t tries = 0; tries < kSettingsRowCount; ++tries) {
    index = (index + step + static_cast<int>(kSettingsRowCount)) %
            static_cast<int>(kSettingsRowCount);
    const auto row = static_cast<SettingsRow>(index);
    if (shown(row)) {
      state_.settingsFocus = row;
      return;
    }
  }
}

FrontAction FrontMenu::open(FrontPage page) noexcept {
  if (page == FrontPage::Guide) {
    state_.guideReturn = state_.page;
  }
  state_.page = page;
  state_.confirmReset = false;
  if (page == FrontPage::HowTo) {
    state_.howToPage = 0;
  } else if (page == FrontPage::Settings) {
    state_.settingsFocus = SettingsRow::Sound;
  }
  return page == FrontPage::Guide ? FrontAction::ShowGuide : FrontAction::Opened;
}

FrontAction FrontMenu::close() noexcept {
  state_.confirmReset = false;
  if (state_.page == FrontPage::Title) {
    return FrontAction::None;
  }
  if (state_.page == FrontPage::Guide) {
    state_.page = state_.guideReturn;
    return state_.page == FrontPage::Title ? FrontAction::Closed : FrontAction::Opened;
  }
  // The focus comes back to the item that opened the page.
  switch (state_.page) {
    case FrontPage::Scores:
      state_.titleFocus = TitleItem::Scores;
      break;
    case FrontPage::HowTo:
      state_.titleFocus = TitleItem::HowTo;
      break;
    case FrontPage::Settings:
      state_.titleFocus = TitleItem::Settings;
      break;
    case FrontPage::Title:
    case FrontPage::Guide:
      break;
  }
  state_.page = FrontPage::Title;
  return FrontAction::Closed;
}

FrontAction FrontMenu::activateItem(TitleItem item) noexcept {
  if (!shown(item)) {
    return FrontAction::None;
  }
  state_.titleFocus = item;
  switch (item) {
    case TitleItem::Continue:
      return FrontAction::Continue;
    case TitleItem::Play:
      return FrontAction::Play;
    case TitleItem::Scores:
      return open(FrontPage::Scores);
    case TitleItem::HowTo:
      return open(FrontPage::HowTo);
    case TitleItem::Settings:
      return open(FrontPage::Settings);
    case TitleItem::About:
      return FrontAction::OpenAbout;
  }
  return FrontAction::None;
}

FrontAction FrontMenu::activateRow(SettingsRow row) noexcept {
  if (!shown(row)) {
    return FrontAction::None;
  }
  state_.settingsFocus = row;
  if (row != SettingsRow::Reset) {
    state_.confirmReset = false;
  }
  switch (row) {
    case SettingsRow::Sound:
      return FrontAction::ToggleSound;
    case SettingsRow::Volume:
      return FrontAction::None;
    case SettingsRow::Music:
      return FrontAction::ToggleMusic;
    case SettingsRow::Haptics:
      return FrontAction::ToggleHaptics;
    case SettingsRow::Reset:
      if (!state_.confirmReset) {
        state_.confirmReset = true;
        return FrontAction::AskReset;
      }
      state_.confirmReset = false;
      return FrontAction::ResetScores;
    case SettingsRow::Back:
      return close();
  }
  return FrontAction::None;
}

FrontAction FrontMenu::command(FrontCommand command) noexcept {
  switch (state_.page) {
    case FrontPage::Title:
      switch (command) {
        case FrontCommand::Up:
          moveTitle(-1);
          return FrontAction::Moved;
        case FrontCommand::Down:
          moveTitle(1);
          return FrontAction::Moved;
        case FrontCommand::Select:
          return activateItem(state_.titleFocus);
        case FrontCommand::Left:
        case FrontCommand::Right:
        case FrontCommand::Back:
          return FrontAction::None;
      }
      break;
    case FrontPage::Scores:
      return command == FrontCommand::Select || command == FrontCommand::Back
                 ? close()
                 : FrontAction::None;
    case FrontPage::HowTo:
      switch (command) {
        case FrontCommand::Left:
        case FrontCommand::Up:
          if (state_.howToPage == 0) {
            return FrontAction::None;
          }
          --state_.howToPage;
          return FrontAction::Moved;
        case FrontCommand::Right:
        case FrontCommand::Down:
          if (state_.howToPage + 1 >= kHowToPageCount) {
            return FrontAction::None;
          }
          ++state_.howToPage;
          return FrontAction::Moved;
        case FrontCommand::Select:
        case FrontCommand::Back:
          return close();
      }
      break;
    case FrontPage::Settings:
      switch (command) {
        case FrontCommand::Up:
          state_.confirmReset = false;
          moveSettings(-1);
          return FrontAction::Moved;
        case FrontCommand::Down:
          state_.confirmReset = false;
          moveSettings(1);
          return FrontAction::Moved;
        case FrontCommand::Left:
        case FrontCommand::Right:
          if (state_.settingsFocus == SettingsRow::Volume) {
            return command == FrontCommand::Left ? FrontAction::VolumeDown
                                                 : FrontAction::VolumeUp;
          }
          return FrontAction::None;
        case FrontCommand::Select:
          return activateRow(state_.settingsFocus);
        case FrontCommand::Back:
          return close();
      }
      break;
    case FrontPage::Guide:
      return close();
  }
  return FrontAction::None;
}

FrontAction FrontMenu::tap(FrontTarget target) noexcept {
  using Kind = FrontTarget::Kind;
  switch (state_.page) {
    case FrontPage::Title:
      if (target.kind == Kind::Item && target.index < kTitleItemCount) {
        return activateItem(static_cast<TitleItem>(target.index));
      }
      return FrontAction::None;
    case FrontPage::Scores:
      return target.kind == Kind::Back ? close() : FrontAction::None;
    case FrontPage::HowTo:
      switch (target.kind) {
        case Kind::Back:
          return close();
        case Kind::Previous:
          return command(FrontCommand::Left);
        case Kind::Next:
          return command(FrontCommand::Right);
        case Kind::GuideButton:
          return config_.guideShown ? open(FrontPage::Guide) : FrontAction::None;
        default:
          return FrontAction::None;
      }
    case FrontPage::Settings:
      switch (target.kind) {
        case Kind::Row:
          if (target.index < kSettingsRowCount) {
            return activateRow(static_cast<SettingsRow>(target.index));
          }
          return FrontAction::None;
        case Kind::VolumeDown:
        case Kind::VolumeUp:
          state_.settingsFocus = SettingsRow::Volume;
          state_.confirmReset = false;
          return target.kind == Kind::VolumeDown ? FrontAction::VolumeDown
                                                 : FrontAction::VolumeUp;
        case Kind::Back:
          return close();
        default:
          return FrontAction::None;
      }
    case FrontPage::Guide:
      return target.kind == Kind::None ? FrontAction::None : close();
  }
  return FrontAction::None;
}

}  // namespace pinball
