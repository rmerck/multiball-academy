#include "core/TableLayout.hpp"

#include <cmath>

namespace pinball::table {

std::vector<Vec2> outerBoundary() {
  constexpr int kArchSegments = 48;

  std::vector<Vec2> points;
  points.reserve(static_cast<std::size_t>(kArchSegments) + 3U);
  points.push_back({kWallLeft, kFloorY});
  points.push_back({kWallRight, kFloorY});
  points.push_back({kWallRight, kArchCenterY});
  // Over the top from the right wall to the left wall.
  for (int i = 1; i < kArchSegments; ++i) {
    const float angle = kPi * static_cast<float>(i) / static_cast<float>(kArchSegments);
    points.push_back({kArchCenterX + kArchRadiusX * std::cos(angle),
                      kArchCenterY - kArchRadiusY * std::sin(angle)});
  }
  points.push_back({kWallLeft, kArchCenterY});
  return points;
}

}  // namespace pinball::table
