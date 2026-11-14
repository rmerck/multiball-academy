#pragma once

#include <cstddef>
#include <string_view>

namespace pinball {

// What is left of the old table's missions: the rank ladder, which the high
// score table stores and shows. The missions themselves named features that
// the table no longer has. Courses and the rule for earning rank come with
// the new table's rule set, which replaces this file.
inline constexpr std::size_t kRankCount = 9;

// The name of a rank; the index is clamped into range.
std::string_view rankName(std::size_t rank) noexcept;

}  // namespace pinball
