#include "core/Courses.hpp"

#include <algorithm>

namespace pinball {

std::string_view rankName(std::size_t rank) noexcept {
  return kRankNames[std::min(rank, kRankCount - 1)];
}

}  // namespace pinball
