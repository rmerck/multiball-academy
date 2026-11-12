#pragma once

// Layout of the information panel to the right of the playfield, in canvas
// pixels. Shared by the display code and the panel art.
namespace pinball::panel {

struct Rect {
  float x;
  float y;
  float w;
  float h;
};

inline constexpr float kLeft = 768.0F;
inline constexpr float kWidth = 512.0F;
inline constexpr float kHeight = 960.0F;
inline constexpr float kInset = 24.0F;
inline constexpr float kWellWidth = kWidth - 2.0F * kInset;

// Recessed display windows, top to bottom.
inline constexpr Rect kScoreWell{kLeft + kInset, 150.0F, kWellWidth, 110.0F};
inline constexpr Rect kStatusWell{kLeft + kInset, 280.0F, kWellWidth, 70.0F};
inline constexpr Rect kRankWell{kLeft + kInset, 370.0F, kWellWidth, 90.0F};
inline constexpr Rect kMissionWell{kLeft + kInset, 480.0F, kWellWidth, 170.0F};
inline constexpr Rect kMessageWell{kLeft + kInset, 670.0F, kWellWidth, 110.0F};
inline constexpr Rect kHighScoreWell{kLeft + kInset, 800.0F, kWellWidth, 70.0F};

inline constexpr Rect kWells[] = {kScoreWell, kStatusWell,  kRankWell,
                                  kMissionWell, kMessageWell, kHighScoreWell};

}  // namespace pinball::panel
