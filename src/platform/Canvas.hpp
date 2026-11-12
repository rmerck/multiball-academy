#pragma once

namespace pinball {

// Logical canvas everything is drawn on. 4:3 like the original, at twice
// 640x480. The playfield occupies the left 768 pixels, the panel the rest.
inline constexpr int kCanvasWidth = 1280;
inline constexpr int kCanvasHeight = 960;

}  // namespace pinball
