#include <array>
#include <cmath>
#include <span>
#include <vector>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/Polygon.hpp"
#include "core/TableLayout.hpp"

// core/Polygon.hpp: the triangles cover the polygon exactly, and a polygon
// that is not simple is refused, not half drawn.
namespace {

using pinball::Vec2;
namespace polygon = pinball::polygon;
namespace table = pinball::table;

float trianglesArea(const std::vector<polygon::Triangle>& triangles) {
  float sum = 0.0F;
  for (const polygon::Triangle& t : triangles) {
    sum += std::fabs(polygon::doubleArea(t)) * 0.5F;
  }
  return sum;
}

bool covers(std::span<const Vec2> outline) {
  const auto triangles = polygon::triangulate(outline);
  if (!triangles) {
    return false;
  }
  const float area = std::fabs(polygon::doubleArea(outline)) * 0.5F;
  return triangles->size() == outline.size() - 2 &&
         std::fabs(trianglesArea(*triangles) - area) <= area * 1e-4F;
}

}  // namespace

void testPolygon() {
  CHECK(covers(table::kShell));
  CHECK(covers(table::kLaneIsland));
  const std::vector<Vec2> outside = polygon::outsideOfShell();
  CHECK(!outside.empty());
  CHECK(covers(outside));
  // The outside and the shell's part of the frame make the whole frame.
  const auto inFrame = [](std::span<const Vec2> outline) {
    std::vector<Vec2> clipped;
    for (std::size_t k = 0; k < outline.size(); ++k) {
      const Vec2 a = outline[k];
      const Vec2 b = outline[(k + 1) % outline.size()];
      if (a.y <= table::kHeight) {
        clipped.push_back(a);
      }
      if ((a.y > table::kHeight) != (b.y > table::kHeight)) {
        const float t = (table::kHeight - a.y) / (b.y - a.y);
        clipped.push_back({a.x + (b.x - a.x) * t, table::kHeight});
      }
    }
    return std::fabs(polygon::doubleArea(clipped)) * 0.5F;
  };
  const float frame = table::kWidth * table::kHeight;
  CHECK(std::fabs(std::fabs(polygon::doubleArea(outside)) * 0.5F + inFrame(table::kShell) - frame) <=
        frame * 1e-4F);
  // Refused rather than half drawn: too few points, or no ear to clip (all
  // on one line).
  const std::array<Vec2, 4> line{{{0.0F, 0.0F}, {10.0F, 0.0F}, {20.0F, 0.0F}, {30.0F, 0.0F}}};
  CHECK(!polygon::triangulate(line).has_value());
  CHECK(!polygon::triangulate(std::span<const Vec2>(line).first(2)).has_value());
}
