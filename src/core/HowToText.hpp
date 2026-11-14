#pragma once

#include <array>
#include <string_view>

#include "core/FrontMenu.hpp"

// The pages of HOW TO PLAY: a short guide drawn from docs/RULES.md, in the
// display's capitals. A line feed starts a new paragraph. Only the first
// page differs between a touch screen and a keyboard. Every page fits the
// body of the smallest layout (tests/FrontTest.cpp).
namespace pinball::howto {

struct Page {
  std::string_view title;
  std::string_view touch;     // On a touch screen.
  std::string_view keyboard;  // With a keyboard; empty: the same as touch.
};

inline constexpr std::array<Page, kHowToPageCount> kPages{{
    {"CONTROLS",
     "LEFT HALF OF THE SCREEN: LEFT FLIPPER. RIGHT HALF: RIGHT FLIPPER.\n"
     "HOLD LAUNCH TO PULL THE PLUNGER; LET GO TO SHOOT. A LONGER PULL SHOOTS HARDER.\n"
     "THE SIDE KEYS NUDGE THE TABLE, AND BUMP NUDGES IT UP. TWO QUICK NUDGES WARN; "
     "THREE TILT.\n"
     "THE FLIPPERS ALSO MOVE THE LIT LANES AT THE TOP.",
     "Z AND /: FLIPPERS.\n"
     "SPACE: HOLD TO PULL THE PLUNGER, LET GO TO SHOOT.\n"
     "X AND .: NUDGE FROM THE LEFT AND RIGHT. UP: NUDGE FROM BELOW. THREE QUICK NUDGES TILT.\n"
     "F2 NEW GAME. F3 PAUSE. ESC IN A PAUSE: MAIN MENU. F1 ABOUT. M SOUND. N MUSIC. "
     "F4 FULL SCREEN."},
    {"THE SHOTS",
     "CIRCUITS: THE OUTER LANES ROUND THE ARCH.\n"
     "RUNWAY AND SKYWAY: THE TWO RAMPS.\n"
     "HANGAR: BEHIND THREE BAY DOORS. LOCKS BALLS.\n"
     "BRIEFING ROOM: THE SCOOP IN THE MIDDLE. STARTS COURSES.\n"
     "TURBINE: THE SPINNER LANE.\n"
     "A BLINKING ARROW MEANS SHOOT THIS NOW.",
     {}},
    {"COURSES AND RANK",
     "SHOOT THE LIT BRIEFING ROOM TO START THE COURSE ON OFFER. BUMPERS CHANGE THE OFFER.\n"
     "FINISH ITS TASK BEFORE THE CLOCK RUNS OUT. A FAILED COURSE CAN BE TAKEN AGAIN.\n"
     "EACH COURSE PASSED RAISES YOUR RANK, FROM ROOKIE UP TO INSTRUCTOR. PASS ALL SIX FOR "
     "GRADUATION DAY.",
     {}},
    {"MULTIBALL",
     "ACADEMY: HIT ALL THREE BAY DOORS, THEN LOCK A BALL IN THE HANGAR. THE THIRD BALL IN "
     "STARTS THREE-BALL MULTIBALL.\n"
     "TURBO: BUMPERS, SPINS AND TURBO TARGETS FILL THE METER. FULL, A SECOND BALL LAUNCHES.\n"
     "IN A MULTIBALL THE RAMPS ARE LIT FOR JACKPOTS.",
     {}},
    {"BONUS AND SAVES",
     "COMBO: FLOW SHOTS WITHIN 4 SECONDS OF EACH OTHER.\n"
     "OVERDRIVE: LIGHT ALL THREE TOP LANES, THEN SHOOT THE TURBINE. DOUBLE SCORES FOR "
     "20 SECONDS.\n"
     "A BALL LOST IN ITS FIRST 15 SECONDS IS SAVED. THE KICKBACK IN THE LEFT OUTLANE "
     "RETURNS ONE BALL; W-I-N-G RELIGHTS IT.",
     {}},
    {"GRADUATION DAY",
     "PASS ALL SIX COURSES IN ONE GAME, THEN SHOOT THE BRIEFING ROOM. THREE BALLS, AND THE "
     "FINAL EXAM: MAKE ALL SEVEN MAJOR SHOTS.\n"
     "THEN THE HANGAR FOR COMMENCEMENT AND THE RANK OF CHANCELLOR.\n"
     "YOU HAVE THREE BALLS. GOOD LUCK.",
     {}},
}};

// The touch guide's labels, one beside each control (screen::GuideLayout).
inline constexpr std::string_view kGuideTitle = "TOUCH CONTROLS";
inline constexpr std::string_view kGuideLeft = "LEFT FLIPPER: HOLD ANYWHERE ON THE LEFT HALF";
inline constexpr std::string_view kGuideRight = "RIGHT FLIPPER: HOLD ANYWHERE ON THE RIGHT HALF";
inline constexpr std::string_view kGuideCentre = "LAUNCH: HOLD TO PULL, LET GO TO SHOOT";
inline constexpr std::string_view kGuideNudge = "NUDGE";
inline constexpr std::string_view kGuidePause = "PAUSE AND MENU";
inline constexpr std::string_view kGuideStart = "TAP ANYWHERE TO PLAY";
inline constexpr std::string_view kGuideBack = "TAP ANYWHERE TO GO BACK";

// The text of a page for a screen with or without touch.
[[nodiscard]] constexpr std::string_view text(const Page& page, bool touch) noexcept {
  return touch || page.keyboard.empty() ? page.touch : page.keyboard;
}

}  // namespace pinball::howto
