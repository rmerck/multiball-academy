#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/PanelLayout.hpp"

// How much text each display window holds. The display draws a fixed-width
// font, so a line fits when it is short enough: these limits are worked out
// from the display windows (strip:: in core/ScreenLayout.hpp, panel:: in
// core/PanelLayout.hpp) and checked when the game is compiled, so a name or
// message that would run off the display never reaches a player.
namespace pinball::display {

// The font: upper case, digits and punctuation, one advance per character.
// tests/MissionTest.cpp holds these to the font the display draws with.
inline constexpr char kFirstChar = ' ';
inline constexpr char kLastChar = '_';
inline constexpr int kAdvance = 6;  // Font pixels per character, gap included.

// Text is drawn at a multiple of the font's pixels, inside a margin. Every
// line may be drawn as small as kSmallestSize (screen::kHudTextSize: 8 points
// at the smallest scene), and the limits are worked out at that size, in the
// narrower of the two displays' windows.
inline constexpr float kPad = 12.0F;
inline constexpr float kSmallestSize = screen::kHudTextSize;

// Characters that fit a width: the last one needs no trailing gap.
constexpr std::size_t charsThatFit(float width, float pixelSize) noexcept {
  if (!(width > 0.0F) || !(pixelSize > 0.0F)) {
    return 0;
  }
  return static_cast<std::size_t>((width / pixelSize + 1.0F) / static_cast<float>(kAdvance));
}

constexpr float innerWidth(const panel::Rect& well) noexcept { return well.w - 2.0F * kPad; }
constexpr std::size_t charsThatFit(const panel::Rect& well) noexcept {
  return charsThatFit(innerWidth(well), kSmallestSize);
}
constexpr std::size_t smaller(std::size_t a, std::size_t b) noexcept { return a < b ? a : b; }

// The rank name: the strip's rank window is the narrowest place it is shown
// (the panel's and the high score table are wider).
inline constexpr std::size_t kMaxRankNameLength =
    smaller(charsThatFit(strip::kRankWell), charsThatFit(panel::kRankWell));
// The course name shares its line with the course's clock, two digits and a
// space.
inline constexpr std::size_t kClockLength = 2;
inline constexpr std::size_t kMaxMissionNameLength =
    smaller(charsThatFit(strip::kCourseWell), charsThatFit(panel::kCourseWell)) - kClockLength - 1;
// The goal line has the line to itself; a count beside it ("2/3") is drawn
// where there is room, and the progress bar under it always shows the same.
inline constexpr std::size_t kMaxGoalTextLength =
    smaller(charsThatFit(strip::kCourseWell), charsThatFit(panel::kCourseWell));
// A message is drawn as large as fits, down to the smallest size.
inline constexpr std::size_t kMaxMessageLength =
    smaller(charsThatFit(strip::kMessageWell), charsThatFit(panel::kMessageWell));

static_assert(kMaxRankNameLength == 10 && kMaxMissionNameLength == 23 &&
                  kMaxGoalTextLength == 26 && kMaxMessageLength == 29,
              "the display windows changed: check every name and message against the new limits");

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

// A line for the message window that is put together while the game runs: a
// message, or a label with a number after it. It owns its characters, so it
// can be kept after what it was made from has gone. It never holds more
// than the window shows.
class Text {
 public:
  constexpr Text() noexcept = default;
  constexpr Text(Message message) noexcept {  // NOLINT(google-explicit-constructor)
    append(message.text());
  }
  [[nodiscard]] constexpr std::string_view view() const noexcept {
    return std::string_view(chars_.data(), size_);
  }
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
  // Adds to the end. What the window cannot show is left off; the makers
  // below are checked when the game is compiled so that nothing ever is.
  constexpr void append(std::string_view text) noexcept {
    for (const char c : text) {
      if (size_ < chars_.size()) {
        chars_[size_++] = c;
      }
    }
  }

 private:
  std::array<char, kMaxMessageLength> chars_{};
  std::size_t size_ = 0;
};

// The largest number a line shows, and its length with its commas.
inline constexpr std::uint64_t kMaxNumber = 999'999'999'999ULL;
inline constexpr std::size_t kMaxNumberLength = 15;

// The words before a number ("SWITCHES 31,250"). It can only be made from
// words that leave room for the gap and the longest number.
class NumberLabel {
 public:
  consteval NumberLabel(const char* text, std::size_t gap = 1)  // NOLINT(google-explicit-constructor)
      : text_(text), gap_(gap) {
    if (gap < 1 || gap > 2 ||
        !fits(text_, kMaxMessageLength - gap - kMaxNumberLength)) {
      messageDoesNotFitTheDisplay();
    }
  }
  constexpr std::string_view text() const noexcept { return text_; }
  constexpr std::size_t gap() const noexcept { return gap_; }

 private:
  std::string_view text_;
  std::size_t gap_;
};

// "LABEL 1,234,567". A number over kMaxNumber is shown as kMaxNumber.
constexpr Text numbered(NumberLabel label, std::uint64_t value) noexcept {
  Text line;
  line.append(label.text());
  line.append(std::string_view("  ").substr(0, label.gap()));
  if (value > kMaxNumber) {
    value = kMaxNumber;
  }
  // Digits from the right, a comma before every third.
  std::array<char, kMaxNumberLength> digits{};
  std::size_t at = digits.size();
  std::size_t count = 0;
  do {
    if (count != 0 && count % 3 == 0) {
      digits[--at] = ',';
    }
    digits[--at] = static_cast<char>('0' + value % 10);
    value /= 10;
    ++count;
  } while (value != 0);
  line.append(std::string_view(digits.data() + at, digits.size() - at));
  return line;
}

static_assert(numbered("SWITCHES", 31250).view() == "SWITCHES 31,250");
static_assert(numbered(NumberLabel("BONUS 5X", 2), 981250).view() == "BONUS 5X  981,250");
static_assert(numbered("X", 0).view() == "X 0" && numbered("X", 999).view() == "X 999" &&
              numbered("X", 1000).view() == "X 1,000");
static_assert(numbered("X", ~std::uint64_t{0}).view() == "X 999,999,999,999");
static_assert(fits(numbered("THIRTEEN CHAR", ~std::uint64_t{0}).view(), kMaxMessageLength));

}  // namespace pinball::display
