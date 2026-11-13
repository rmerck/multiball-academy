#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/AboutPages.hpp"
#include "core/HighScores.hpp"
#include "core/ScreenLayout.hpp"

namespace pinball {

// Draws the About screen in layout.dialog, over a dimmed canvas. Three
// pages: the high score table; the version and what the game does with the
// player's data (nothing), with the address of the privacy policy if the
// build has one (kPrivacyUrl) and no mention of one if it has not; and the
// licence notices, which take licencePageCount pages of their own. A
// licencePage past the last shows the last. Body text is drawn at
// screen::smallestTextSize, so the pages are more on a phone.
//
// A touch screen gets the controls of screen::aboutLayout: three tabs, a
// close button, and where they apply the privacy button and the two
// page-turning buttons. A desktop gets the dialog as it has always been,
// with a line saying that the arrow keys turn the pages: left and right
// step through scores, about and each licence page in turn
// (turnAboutPage in core/AboutPages.hpp).
void drawAbout(SDL_Renderer* renderer, const HighScores& scores, const screen::Layout& layout,
               const AboutState& state);

// How many pages the licence text takes in this layout. At least 1.
[[nodiscard]] std::size_t licencePageCount(const screen::Layout& layout) noexcept;
// The notices of the game and of the libraries built into it.
[[nodiscard]] std::string_view licenceText() noexcept;

// True if drawAbout shows the privacy button for this state, so a tap in
// AboutLayout::privacyLink should open kPrivacyUrl: the About page of a
// touch screen, once the build has an address for the policy.
[[nodiscard]] bool privacyButtonShown(const screen::Layout& layout,
                                      const AboutState& state) noexcept;
// True if drawAbout shows the two page-turning buttons for this state: the
// licences of a touch screen, when they take more than one page. The one
// that leads nowhere (back from the first, on from the last) is drawn
// greyed.
[[nodiscard]] bool pageButtonsShown(const screen::Layout& layout,
                                    const AboutState& state) noexcept;

}  // namespace pinball
