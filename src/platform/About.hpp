#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/HighScores.hpp"
#include "core/ScreenLayout.hpp"

namespace pinball {

enum class AboutPage : std::uint8_t { Scores, About, Licences };

// Which page of the About screen is up, and how far into the licences.
struct AboutState {
  AboutPage page = AboutPage::Scores;
  std::size_t licencePage = 0;
};

// Draws the About screen in layout.dialog, over a dimmed canvas: the game's
// title and version, where the source is, and the high score table.
//
// Only that one page exists so far, and it is drawn whatever the state says.
void drawAbout(SDL_Renderer* renderer, const HighScores& scores, const screen::Layout& layout,
               const AboutState& state);

// How many pages the licence text takes in this layout. At least 1.
[[nodiscard]] std::size_t licencePageCount(const screen::Layout& layout) noexcept;
// The notices of the game and of the libraries built into it. Empty until
// the build generates them.
[[nodiscard]] std::string_view licenceText() noexcept;

}  // namespace pinball
