#pragma once

namespace pinball {

// A point or vector in playfield pixels: x to the right, y down.
struct Vec2 {
  float x = 0.0F;
  float y = 0.0F;
};

constexpr Vec2 operator+(Vec2 a, Vec2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
constexpr Vec2 operator-(Vec2 a, Vec2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
constexpr Vec2 operator*(Vec2 a, float s) noexcept { return {a.x * s, a.y * s}; }

}  // namespace pinball
