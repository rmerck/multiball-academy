#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

// Which page of the About screen is up, and how the pages follow each
// other. No SDL: the keyboard, the touch screen and the tests share it.
namespace pinball {

enum class AboutPage : std::uint8_t { Scores, About, Licences };

struct AboutState {
  AboutPage page = AboutPage::Scores;
  // How far into the licences, from 0. Only looked at on that page.
  std::size_t licencePage = 0;
};

// The pages in a row: scores, about, then each licence page.
[[nodiscard]] constexpr std::size_t aboutPageTotal(std::size_t licencePages) noexcept {
  return 2 + std::max<std::size_t>(licencePages, 1);
}

// Where a state is in that row, from 0. A licence page past the last counts
// as the last.
[[nodiscard]] constexpr std::size_t aboutPageNumber(const AboutState& state,
                                                    std::size_t licencePages) noexcept {
  switch (state.page) {
    case AboutPage::Scores:
      return 0;
    case AboutPage::About:
      return 1;
    case AboutPage::Licences:
      break;
  }
  return 2 + std::min(state.licencePage, std::max<std::size_t>(licencePages, 1) - 1);
}

// One page on (forward) or back. It stops at either end.
[[nodiscard]] constexpr AboutState turnAboutPage(const AboutState& state, bool forward,
                                                 std::size_t licencePages) noexcept {
  const std::size_t total = aboutPageTotal(licencePages);
  std::size_t number = aboutPageNumber(state, licencePages);
  if (forward && number + 1 < total) {
    ++number;
  } else if (!forward && number > 0) {
    --number;
  }
  if (number == 0) {
    return {AboutPage::Scores, 0};
  }
  if (number == 1) {
    return {AboutPage::About, 0};
  }
  return {AboutPage::Licences, number - 2};
}

}  // namespace pinball
