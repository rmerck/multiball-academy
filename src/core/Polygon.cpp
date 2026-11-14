#include "core/Polygon.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>

#include "core/TableLayout.hpp"

namespace pinball::polygon {

float doubleArea(std::span<const Vec2> outline) noexcept {
  float sum = 0.0F;
  for (std::size_t i = 0; i < outline.size(); ++i) {
    const Vec2 a = outline[i];
    const Vec2 b = outline[(i + 1) % outline.size()];
    sum += a.x * b.y - b.x * a.y;
  }
  return sum;
}

std::optional<std::vector<Triangle>> triangulate(std::span<const Vec2> outline) {
  std::vector<Vec2> points(outline.begin(), outline.end());
  std::vector<Triangle> out;
  if (points.size() < 3) {
    return std::nullopt;
  }
  out.reserve(points.size() - 2);
  const auto cross = [](Vec2 o, Vec2 a, Vec2 b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
  };
  const float turn = doubleArea(points) >= 0.0F ? 1.0F : -1.0F;
  while (points.size() > 3) {
    bool clipped = false;
    for (std::size_t i = 0; i < points.size() && !clipped; ++i) {
      const Vec2 a = points[(i + points.size() - 1) % points.size()];
      const Vec2 b = points[i];
      const Vec2 c = points[(i + 1) % points.size()];
      if (cross(a, b, c) * turn <= 0.0F) {
        continue;  // A reflex corner is not an ear.
      }
      bool empty = true;
      for (const Vec2 p : points) {
        const bool corner = (p.x == a.x && p.y == a.y) || (p.x == b.x && p.y == b.y) ||
                            (p.x == c.x && p.y == c.y);
        if (!corner && cross(a, b, p) * turn >= 0.0F && cross(b, c, p) * turn >= 0.0F &&
            cross(c, a, p) * turn >= 0.0F) {
          empty = false;
          break;
        }
      }
      if (empty) {
        out.push_back({a, b, c});
        points.erase(points.begin() + static_cast<std::ptrdiff_t>(i));
        clipped = true;
      }
    }
    if (!clipped) {
      return std::nullopt;  // Not a simple polygon.
    }
  }
  out.push_back({points[0], points[1], points[2]});
  return out;
}

std::vector<Vec2> outsideOfShell() {
  std::vector<Vec2> outline{{0.0F, table::kHeight}, {0.0F, 0.0F}, {table::kWidth, 0.0F},
                            {table::kWidth, table::kHeight}};
  const auto& shell = table::kShell;
  const std::size_t n = shell.size();
  std::vector<Vec2> inside;
  int crossings = 0;
  for (std::size_t k = 0; k < n; ++k) {
    const Vec2 a = shell[k];
    const Vec2 b = shell[(k + 1) % n];
    if (a.y <= table::kHeight) {
      inside.push_back(a);
    }
    if ((a.y > table::kHeight) != (b.y > table::kHeight)) {
      const float t = (table::kHeight - a.y) / (b.y - a.y);
      inside.push_back({a.x + (b.x - a.x) * t, table::kHeight});
      ++crossings;
    }
  }
  if (crossings != 2) {
    return {};
  }
  // Start at the right-hand crossing, so the shell runs back to the left.
  std::size_t start = 0;
  for (std::size_t k = 0; k < inside.size(); ++k) {
    if (inside[k].y == table::kHeight && inside[k].x > table::kWidth * 0.5F) {
      start = k;
    }
  }
  std::rotate(inside.begin(), inside.begin() + static_cast<std::ptrdiff_t>(start), inside.end());
  outline.insert(outline.end(), inside.begin(), inside.end());
  return outline;
}

}  // namespace pinball::polygon
