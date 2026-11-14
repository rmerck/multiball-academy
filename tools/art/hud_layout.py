# PROPOSED display windows for the HUD rework that comes with the table's real rules. Not used to draw
# anything yet: make_ui.py draws the windows where the game has them today (layout.json). The HUD change
# moves these numbers into strip:: (src/core/ScreenLayout.hpp) and panel:: (src/core/PanelLayout.hpp),
# then regenerates the strip and panel art, which follows the headers through layout.json.
# Units; strip rectangles are relative to the strip's origin, panel rectangles are canvas coordinates of
# the Classic composition (panel left = 768). Buttons are unchanged.
STRIP = {
    # Every line of text in the strip is at least 17 units high: 8 pt at the smallest phone (0.49 pt per unit).
    # Wells are 12 units apart. There are no decorative labels: icons and position say what a meter is.
    "pauseButton": (16, 8, 88, 84),      # unchanged
    "scoreWell": (116, 10, 400, 80),     # the score alone, digits about 36 units high
    "statusWell": (528, 10, 224, 80),    # line 1: BALL 2   line 2: multiplier, ball-save lamp
    "rankWell": (16, 102, 172, 70),      # rank name (10 characters), seven pips under it
    "courseWell": (200, 102, 412, 70),   # course name, goal line (26 characters need 390), timer bar along the foot
    "meterWell": (624, 102, 128, 70),    # top: three doors and two bays; bottom: turbo meter. Icons only
    "messageWell": (16, 184, 520, 40),   # one line; 20 characters need 292 units
    "lampWell": (548, 184, 204, 40),     # six course lamps and the graduation lamp
}
PANEL = {
    "title": (792, 20, 368, 116),        # touch title area; desktop uses 792,20,464,116
    "pauseButton": (1176, 20, 80, 80),
    "scoreWell": (792, 150, 464, 100),
    "statusWell": (792, 262, 464, 56),
    "rankWell": (792, 330, 464, 84),
    "courseWell": (792, 426, 464, 150),  # name, goal, timer bar, then the row of course lamps
    "meterWell": (792, 588, 464, 84),    # LOCK, TURBO, JACKPOT
    "messageWell": (792, 684, 464, 96),
    "highScoreWell": (792, 792, 464, 70),
    "controls": (792, 876, 464, 76),     # unchanged: nudge, centre, nudge, flipper corner
}
