#pragma once

#include "core/ScreenLayout.hpp"

// Layout of the information panel to the right of the playfield, in the
// units of the Classic composition (core/ScreenLayout.hpp), whose top left
// corner is the playfield's. Shared by the display code and the panel art.
namespace pinball::panel {

using Rect = screen::Rect;

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

// Every window lies inside the panel, and they run down it without touching.
constexpr bool wellsAreLaidOut() noexcept {
  float floor = 0.0F;
  for (const Rect& well : kWells) {
    if (well.w <= 0.0F || well.h <= 0.0F || well.x < kLeft || well.x + well.w > kLeft + kWidth ||
        well.y < floor || well.y + well.h > kHeight) {
      return false;
    }
    floor = well.y + well.h;
  }
  return true;
}
static_assert(wellsAreLaidOut());

// Above the wells: the title. Below them: the key hints on a desktop, the
// button row on a touch screen.
inline constexpr Rect kTitleArea{kLeft + kInset, 20.0F, kWellWidth, 116.0F};
inline constexpr Rect kControlsArea{kLeft + kInset, 876.0F, kWellWidth, 76.0F};

// Touch controls of the Classic composition. The corner to the right of the
// row stays free for the right thumb's flipper.
inline constexpr Rect kNudgeLeftButton{792.0F, 876.0F, 96.0F, 76.0F};
inline constexpr Rect kCentreButton{896.0F, 876.0F, 132.0F, 76.0F};
inline constexpr Rect kNudgeRightButton{1036.0F, 876.0F, 96.0F, 76.0F};
inline constexpr Rect kPauseButton{1176.0F, 20.0F, 80.0F, 80.0F};

}  // namespace pinball::panel
