#pragma once

#include <array>
#include <optional>
#include <span>
#include <vector>

#include "core/Vec2.hpp"

// Simple polygons for the drawing: filled by clipping ears, and the outline
// of what lies outside the playfield's shell. No SDL: unit tested in
// core_tests.
namespace pinball::polygon {

using Triangle = std::array<Vec2, 3>;

// Twice the signed area (positive anticlockwise in y-up terms).
[[nodiscard]] float doubleArea(std::span<const Vec2> outline) noexcept;

// The polygon as triangles, either winding. Empty optional if it is not a
// simple polygon (no ear could be clipped): the caller decides how loudly
// to fail.
[[nodiscard]] std::optional<std::vector<Triangle>> triangulate(std::span<const Vec2> outline);

// What lies outside the shell within the playfield's frame
// (0..table::kWidth, 0..table::kHeight), as one simple polygon: the frame
// down to its bottom edge and the shell back the other way (the shell runs
// below the bottom edge, so the outside is not a ring). Empty if the shell
// does not cross the bottom edge twice.
[[nodiscard]] std::vector<Vec2> outsideOfShell();

}  // namespace pinball::polygon
