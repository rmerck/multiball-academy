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
     "LOOPS: THE OUTER ORBITS ROUND THE ARCH.\n"
     "RUNWAY AND SKYWAY: THE TWO RAMPS. LIT, THE SKYWAY SENDS THE BALL TO THE FLIGHT DECK.\n"
     "HANGAR: BEHIND THREE BAY DOORS. LOCKS BALLS.\n"
     "MISSION: THE SCOOP RIGHT OF CENTRE. STARTS SORTIES.\n"
     "AFTERBURNER: THE CENTRE LANE UNDER THE BANDIT DRONE.\n"
     "A BLINKING ARROW MEANS SHOOT THIS NOW.",
     {}},
    {"SORTIES AND RANK",
     "EACH SORTIE IS LIT BY ITS OWN SHOTS: LOOPS, RAMPS, BANDIT HITS, T-A-X-I, THE NEST "
     "TARGETS, 3G ON THE CAPTIVE BALL. SHOOT THE MISSION TO FLY IT.\n"
     "FINISH ITS TASK BEFORE THE CLOCK RUNS OUT. EACH ONE FLOWN RAISES YOUR RANK AND YOUR "
     "ALTITUDE: THE TABLE GETS FASTER AND HARDER.",
     {}},
    {"MULTIBALL",
     "SQUADRON: HIT ALL THREE BAY DOORS, THEN LOCK BALLS IN THE HANGAR. THE THIRD BALL IN "
     "STARTS THREE-BALL MULTIBALL. A CAT SHOT ON THE FLIGHT DECK LOCKS A BALL TOO.\n"
     "SCRAMBLE: JETS, SPINS, BANDIT HITS AND CAT SHOTS FILL THE BOOST METER. FULL, A SECOND "
     "BALL LAUNCHES.\n"
     "RAMPS AND LOOPS ARE LIT FOR JACKPOTS. BANDIT DOWN LIGHTS THE SUPER JACKPOT.",
     {}},
    {"BONUS AND SAVES",
     "COMBO: FLOW SHOTS WITHIN 4 SECONDS OF EACH OTHER.\n"
     "OVERDRIVE: LIGHT R-A-D, THEN SHOOT THE AFTERBURNER. DOUBLE SCORES FOR 20 SECONDS.\n"
     "F-L-A-P-S RELIGHTS THE MISSION AND ADDS BONUS X. A BALL LOST IN ITS FIRST 10 SECONDS "
     "IS SAVED. THE KICKBACK RETURNS ONE BALL; W-I-N-G RELIGHTS IT.",
     {}},
    {"WINGS AND AIR SHOW",
     "FLY ALL SIX SORTIES FOR THE WINGS CEREMONY: 60 SECONDS, EVERY SHOT 2X.\n"
     "WIN ALL SIX, PLAY BOTH MULTIBALLS AND SPELL A-C-E FOR THE AIR SHOW: THREE BALLS, EVERY "
     "SHOT ONCE, THEN KNOCK DOWN THE BANDIT AND SHOOT THE HANGAR.\n"
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
