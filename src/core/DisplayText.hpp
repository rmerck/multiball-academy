#pragma once

#include <cstddef>
#include <string_view>

#include "core/PanelLayout.hpp"

// How much text each display window holds. The display draws a fixed-width
// font, so a line fits when it is short enough: these limits are worked out
// from the panel windows and checked when the game is compiled, so a name or
// message that would run off the display never reaches a player.
namespace pinball::display {

// The font: upper case, digits and punctuation, one advance per character.
// tests/MissionTest.cpp holds these to the font the display draws with.
inline constexpr char kFirstChar = ' ';
inline constexpr char kLastChar = '_';
inline constexpr int kAdvance = 6;  // Font pixels per character, gap included.

// Text is drawn at a whole multiple of the font's pixels, inside a margin.
inline constexpr float kPad = 12.0F;
inline constexpr float kRankSize = 4.0F;
inline constexpr float kMissionSize = 3.0F;
inline constexpr float kSmallestSize = 2.0F;  // Goal lines, and a long message.

// Characters that fit a width: the last one needs no trailing gap.
constexpr std::size_t charsThatFit(float width, float pixelSize) noexcept {
  if (!(width > 0.0F) || !(pixelSize > 0.0F)) {
    return 0;
  }
  return static_cast<std::size_t>((width / pixelSize + 1.0F) / static_cast<float>(kAdvance));
}

constexpr float innerWidth(const panel::Rect& well) noexcept { return well.w - 2.0F * kPad; }

// The rank name, at its one size.
inline constexpr std::size_t kMaxRankNameLength =
    charsThatFit(innerWidth(panel::kRankWell), kRankSize);
// The mission name, at its one size.
inline constexpr std::size_t kMaxMissionNameLength =
    charsThatFit(innerWidth(panel::kMissionWell), kMissionSize);
// A goal shares its line with the count still to go ("255 TO GO" at most)
// and a space between them.
inline constexpr std::size_t kCountLength = 9;
inline constexpr std::size_t kMaxGoalTextLength =
    charsThatFit(innerWidth(panel::kMissionWell), kSmallestSize) - kCountLength - 1;
// A message shrinks to fit, down to the smallest size.
inline constexpr std::size_t kMaxMessageLength =
    charsThatFit(innerWidth(panel::kMessageWell), kSmallestSize);

static_assert(kMaxRankNameLength == 18 && kMaxMissionNameLength == 24 &&
              kMaxGoalTextLength == 26 && kMaxMessageLength == 36,
              "the panel changed: check every name and message against the new limits");

// True if the display can show the text whole: not empty, not too long, only
// characters the font has, and no space at either end to put it off centre.
constexpr bool fits(std::string_view text, std::size_t maxLength) noexcept {
  if (text.empty() || text.size() > maxLength || text.front() == ' ' || text.back() == ' ') {
    return false;
  }
  for (const char c : text) {
    if (c < kFirstChar || c > kLastChar) {
      return false;
    }
  }
  return true;
}

// Reached only when a message does not fit: it is not a constant expression,
// so the build stops at the message.
void messageDoesNotFitTheDisplay();

// A line for the message window. It can only be made from text that fits,
// and that is decided by the compiler.
class Message {
 public:
  consteval Message(const char* text) : text_(text) {  // NOLINT(google-explicit-constructor)
    if (!fits(text_, kMaxMessageLength)) {
      messageDoesNotFitTheDisplay();
    }
  }
  constexpr std::string_view text() const noexcept { return text_; }

 private:
  std::string_view text_;
};

}  // namespace pinball::display
