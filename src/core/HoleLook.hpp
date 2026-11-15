#pragma once

#include <algorithm>

#include "core/TableLayout.hpp"

namespace pinball {

// How a ball part-way down into a hole is drawn, from BallView::sink (0 on
// the table, 1 all the way down): the size it is drawn at (a share of its
// own), how bright (a colour multiplier) and how opaque. The game times the
// sink in fixed steps; this only shapes it, so the picture never changes
// what happens.
struct HoleLook {
  float scale = 1.0F;
  float shade = 1.0F;
  float alpha = 1.0F;
};

namespace hole_look {
[[nodiscard]] constexpr float smooth(float t) noexcept {
  const float c = std::clamp(t, 0.0F, 1.0F);
  return c * c * (3.0F - 2.0F * c);
}
}  // namespace hole_look

// Full motion: smaller and darker the further down, fading out over the
// last part of the way. Reduce Motion: never smaller or larger (no zoom),
// and the change is shorter: it darkens and fades only over the middle half
// of the sink, a straight cross-fade.
[[nodiscard]] constexpr HoleLook holeLook(float sink, bool reduceMotion) noexcept {
  const float s = std::clamp(sink, 0.0F, 1.0F);
  if (reduceMotion) {
    const float t = hole_look::smooth((s - 0.25F) * 2.0F);
    return {1.0F, 1.0F - table::kHoleSinkDarken * t, 1.0F - t};
  }
  return {1.0F - table::kHoleSinkShrink * s, 1.0F - table::kHoleSinkDarken * s,
          1.0F - hole_look::smooth((s - 0.7F) / 0.3F)};
}

}  // namespace pinball
