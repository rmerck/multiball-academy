#pragma once

#include <cstddef>
#include <cstdint>

// The screens in front of the game: the title with its menu, the high
// scores, how to play, the settings and the touch guide. What is chosen
// and where the keyboard focus is; what a key or a tap does. No SDL, so the
// keyboard, the touch screen and the tests share it. The About screen is
// the game's own (platform/About.hpp), opened from here.
namespace pinball {

enum class FrontPage : std::uint8_t { Title, Scores, HowTo, Settings, Guide };

// The title's menu, top to bottom. Continue is shown only when a saved
// game exists (FrontConfig::continueShown).
enum class TitleItem : std::uint8_t { Continue, Play, Scores, HowTo, Settings, About };
inline constexpr std::size_t kTitleItemCount = 6;

// The rows of the settings page, top to bottom. Haptics is shown only where
// the device has them to give (FrontConfig::hapticsShown).
enum class SettingsRow : std::uint8_t { Sound, Volume, Music, Haptics, Reset, Back };
inline constexpr std::size_t kSettingsRowCount = 6;

// The pages of HOW TO PLAY.
inline constexpr std::size_t kHowToPageCount = 6;

// What the front screens show, decided by the platform.
struct FrontConfig {
  // A saved game exists to go back to (filled in by the resume feature).
  bool continueShown = false;
  // The device has haptics to switch.
  bool hapticsShown = false;
  // A touch screen: the guide to its controls can be replayed.
  bool guideShown = false;
};

// Keys, or a controller, in front of the game.
enum class FrontCommand : std::uint8_t { Up, Down, Left, Right, Select, Back };

// What the platform must do after a command or a tap. Moved: only the
// focus or the page changed (a click to hear, nothing else to do).
enum class FrontAction : std::uint8_t {
  None,
  Moved,
  Opened,     // Another page is up.
  Closed,     // Back to the title.
  Continue,   // Go back to the saved game.
  Play,       // Start a new game.
  OpenAbout,  // The game's About screen, over the title.
  ToggleSound,
  VolumeDown,
  VolumeUp,
  ToggleMusic,
  ToggleHaptics,
  AskReset,     // The reset button now waits for a second press.
  ResetScores,  // Confirmed: empty the high score table.
  ShowGuide,    // The touch guide is up, from HOW TO PLAY.
};

// One thing a finger can land on in the front screens.
struct FrontTarget {
  enum class Kind : std::uint8_t {
    None,
    Item,         // index: a TitleItem.
    Back,         // The page's back button.
    Previous,     // How to play: the page before,
    Next,         // and the page after.
    GuideButton,  // How to play: show the touch guide.
    Row,          // index: a SettingsRow (toggles, reset).
    VolumeDown,
    VolumeUp,
    Anywhere,     // The touch guide and the title's intro: a tap anywhere.
  };
  Kind kind = Kind::None;
  std::uint8_t index = 0;

  [[nodiscard]] constexpr bool operator==(const FrontTarget&) const noexcept = default;
};

struct FrontState {
  FrontPage page = FrontPage::Title;
  TitleItem titleFocus = TitleItem::Play;
  SettingsRow settingsFocus = SettingsRow::Sound;
  std::size_t howToPage = 0;
  // The reset button was pressed once and waits for a second press.
  bool confirmReset = false;
  // Where the touch guide goes back to when it is dismissed.
  FrontPage guideReturn = FrontPage::HowTo;
};

class FrontMenu {
 public:
  explicit FrontMenu(FrontConfig config = {}) noexcept;

  [[nodiscard]] const FrontState& state() const noexcept { return state_; }
  [[nodiscard]] const FrontConfig& config() const noexcept { return config_; }
  // Shows or hides Continue and Haptics; the focus leaves anything hidden.
  void configure(FrontConfig config) noexcept;
  // Back to the title, focus on its first item. For each return to it.
  void reset() noexcept;

  [[nodiscard]] FrontAction command(FrontCommand command) noexcept;
  // A tap that was let go on the target it went down on.
  [[nodiscard]] FrontAction tap(FrontTarget target) noexcept;

  // Whether an item or a row is on the screen now.
  [[nodiscard]] bool shown(TitleItem item) const noexcept;
  [[nodiscard]] bool shown(SettingsRow row) const noexcept;

 private:
  [[nodiscard]] FrontAction activateItem(TitleItem item) noexcept;
  [[nodiscard]] FrontAction activateRow(SettingsRow row) noexcept;
  [[nodiscard]] FrontAction open(FrontPage page) noexcept;
  [[nodiscard]] FrontAction close() noexcept;
  void moveTitle(int step) noexcept;
  void moveSettings(int step) noexcept;

  FrontConfig config_;
  FrontState state_;
};

}  // namespace pinball
