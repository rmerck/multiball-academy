#pragma once

#include <cstddef>

#include "core/HighScores.hpp"

namespace pinball {

// The three letters a player enters for a high score, and the cursor that
// moves across them. Pure state: the platform layer feeds it key presses
// and draws it.
class InitialsEntry {
 public:
  // Starts from the given letters; anything that is not A to Z becomes A.
  explicit constexpr InitialsEntry(Initials start = {'A', 'A', 'A'}) noexcept : letters_(start) {
    for (char& c : letters_) {
      if (c < 'A' || c > 'Z') {
        c = 'A';
      }
    }
  }

  constexpr const Initials& letters() const noexcept { return letters_; }
  // Index of the letter being edited, 0 to 2.
  constexpr std::size_t cursor() const noexcept { return cursor_; }

  // Steps the current letter forward or back through the alphabet, wrapping.
  constexpr void next() noexcept {
    char& c = letters_[cursor_];
    c = c == 'Z' ? 'A' : static_cast<char>(c + 1);
  }
  constexpr void previous() noexcept {
    char& c = letters_[cursor_];
    c = c == 'A' ? 'Z' : static_cast<char>(c - 1);
  }

  // Sets the current letter and moves on, stopping at the last. Anything
  // but A to Z is ignored.
  constexpr void type(char c) noexcept {
    if (c < 'A' || c > 'Z') {
      return;
    }
    letters_[cursor_] = c;
    right();
  }

  constexpr void left() noexcept {
    if (cursor_ > 0) {
      --cursor_;
    }
  }
  constexpr void right() noexcept {
    if (cursor_ + 1 < letters_.size()) {
      ++cursor_;
    }
  }

  // Confirms the current letter. Returns true when that was the last one
  // and the entry is complete.
  constexpr bool confirm() noexcept {
    if (cursor_ + 1 >= letters_.size()) {
      return true;
    }
    ++cursor_;
    return false;
  }

 private:
  Initials letters_;
  std::size_t cursor_ = 0;
};

}  // namespace pinball
