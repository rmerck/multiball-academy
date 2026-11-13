#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>

#include "core/ScreenLayout.hpp"

// The text form of a window for the review tools: a preset's name or a size
// in points, with an optional output scale and safe-area insets.
//
//   phone-pro            a preset
//   820x1180             a touch window of that many points
//   402x874@3            the same kind, drawn at 3 pixels per point
//   402x874+62,34,0,0    with insets: top, bottom, left, right
//   pad-split@2+24,20,0,0
//
// Parsed as strictly as the game's own arguments: digits only, no signs, no
// spaces, nothing left over, every number in range.
namespace pinball::screen {

struct SurfaceSpec {
  Surface surface;
  Idiom idiom;
  // Pixels per point to draw at. 0: not given, one pixel per canvas unit.
  int scale;
};

inline constexpr std::uint32_t kMinSpecPoints = 100;
inline constexpr std::uint32_t kMaxSpecPoints = 4096;
inline constexpr std::uint32_t kMaxSpecScale = 4;

// A decimal number of one to four digits in [low, high]. Nothing else.
[[nodiscard]] inline std::optional<std::uint32_t> parseSpecNumber(std::string_view text,
                                                                  std::uint32_t low,
                                                                  std::uint32_t high) noexcept {
  if (text.empty() || text.size() > 4) {
    return std::nullopt;
  }
  std::uint32_t value = 0;
  const char* const end = text.data() + text.size();
  const auto result = std::from_chars(text.data(), end, value, 10);
  if (result.ec != std::errc{} || result.ptr != end || value < low || value > high) {
    return std::nullopt;
  }
  return value;
}

// "T,B,L,R" in points, for a surface of the given size. Each inset is
// 0 to 999, and each pair must leave at least a quarter of its side, which
// is as much as computeLayout would honour.
[[nodiscard]] inline bool parseSpecInsets(std::string_view text, Surface& surface) noexcept {
  std::array<float, 4> values{};
  for (std::size_t i = 0; i < values.size(); ++i) {
    const std::size_t comma = text.find(',');
    const bool last = i + 1 == values.size();
    if ((comma == std::string_view::npos) != last) {
      return false;
    }
    const auto value = parseSpecNumber(text.substr(0, comma), 0, 999);
    if (!value) {
      return false;
    }
    values[i] = static_cast<float>(*value);
    text = last ? std::string_view{} : text.substr(comma + 1);
  }
  if (values[0] + values[1] > surface.height * kMaxInsetShare ||
      values[2] + values[3] > surface.width * kMaxInsetShare) {
    return false;
  }
  surface.safeTop = values[0];
  surface.safeBottom = values[1];
  surface.safeLeft = values[2];
  surface.safeRight = values[3];
  return true;
}

// Parses a window. known is the list of presets a name may come from.
[[nodiscard]] inline std::optional<SurfaceSpec> parseSurfaceSpec(
    std::string_view text, std::span<const Preset> known) noexcept {
  if (text.empty() || text.size() > 64) {
    return std::nullopt;
  }
  // Split off "+insets", then "@scale"; what is left names the surface.
  std::string_view insets;
  bool hasInsets = false;
  if (const std::size_t plus = text.find('+'); plus != std::string_view::npos) {
    insets = text.substr(plus + 1);
    text = text.substr(0, plus);
    hasInsets = true;
  }
  SurfaceSpec spec{};
  if (const std::size_t at = text.find('@'); at != std::string_view::npos) {
    const auto scale = parseSpecNumber(text.substr(at + 1), 1, kMaxSpecScale);
    if (!scale) {
      return std::nullopt;
    }
    spec.scale = static_cast<int>(*scale);
    text = text.substr(0, at);
  }
  bool found = false;
  for (const Preset& preset : known) {
    if (preset.name == text) {
      spec.surface = preset.surface;
      spec.idiom = preset.idiom;
      found = true;
      break;
    }
  }
  if (!found) {
    const std::size_t by = text.find('x');
    if (by == std::string_view::npos) {
      return std::nullopt;
    }
    const auto width = parseSpecNumber(text.substr(0, by), kMinSpecPoints, kMaxSpecPoints);
    const auto height = parseSpecNumber(text.substr(by + 1), kMinSpecPoints, kMaxSpecPoints);
    if (!width || !height) {
      return std::nullopt;
    }
    spec.surface = {static_cast<float>(*width), static_cast<float>(*height), 0.0F, 0.0F, 0.0F,
                    0.0F};
    spec.idiom = Idiom::Touch;
  }
  if (hasInsets && !parseSpecInsets(insets, spec.surface)) {
    return std::nullopt;
  }
  return spec;
}

}  // namespace pinball::screen
