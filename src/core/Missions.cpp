#include "core/Missions.hpp"

#include <algorithm>
#include <array>

#include "core/DisplayText.hpp"

namespace pinball {

namespace {

constexpr std::array<std::string_view, kRankCount> kRankNames{
    "ROOKIE", "TRAINEE",    "WINGMATE",    "PILOT",     "NAVIGATOR",
    "ACE",    "INSTRUCTOR", "FLIGHT DEAN", "CHANCELLOR"};

// Every rank name fits wherever a rank is shown.
static_assert(std::ranges::all_of(kRankNames, [](std::string_view name) {
  return display::fits(name, display::kMaxRankNameLength);
}));

}  // namespace

std::string_view rankName(std::size_t rank) noexcept {
  return kRankNames[std::min(rank, kRankCount - 1)];
}

}  // namespace pinball
