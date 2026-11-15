# The single source of colours and art constants for Multiball Academy.
# gen_header.py turns the parts the renderer needs into generated/ArtPalette.hpp.

# ---- master palette -----------------------------------------------------------
INK = "#0A0920"        # outlines, bezels, contact shadows; never pure black
NIGHT = "#12123A"      # backdrop base and letterbox colour
DECK_A = "#141A44"     # deck plate, dark square
DECK_B = "#1A2152"     # deck plate, light square
DECK_HI = "#2C3A7C"    # the most a plate may be lifted by the spot light
WELL = "#080A22"       # display glass
WELL_HI = "#1B2466"    # second dither colour in display wells
VIOLET = "#6A35C9"
ORCHID = "#B98CFF"
TEAL = "#00A7A5"
AQUA = "#63E6D8"
PINK = "#FF2E88"
ORANGE = "#FF6B1A"
YELLOW = "#FFD23C"
GREEN = "#2ED16B"
BLUE = "#2F8CFF"
CREAM = "#FFF4D6"
WHITE = "#FFFFFF"
CH_HI = "#F4FAFF"      # chrome highlight
CH_MID = "#9FB4CC"     # chrome body
CH_LO = "#2B3550"      # chrome shade
GOLD_HI = "#FFFBE0"
GOLD = "#FFD23C"
GOLD_LO = "#A85A10"

# ---- shot and lamp colours (one shot, one colour, everywhere) ------------------
SHOT = {"circuit": AQUA, "runway": ORCHID, "hangar": ORANGE, "briefing": YELLOW, "turbine": GREEN, "skyway": PINK}
# LampId order from TableLayout.hpp. Value: colour the renderer tints the insert and its glow with.
LAMPS = [
    ("ArrowCircuitLeft", AQUA), ("ArrowRunway", ORCHID), ("ArrowHangar", ORANGE), ("ArrowBriefing", YELLOW),
    ("ArrowTurbine", GREEN), ("ArrowSkyway", PINK), ("ArrowCircuitRight", AQUA), ("Lock1", ORANGE), ("Lock2", ORANGE),
    ("Course1", ORCHID), ("Course2", ORCHID), ("Course3", ORCHID), ("Course4", ORCHID), ("Course5", ORCHID),
    ("Course6", ORCHID), ("Graduation", YELLOW), ("ExtraCredit", AQUA), ("ExtraBall", PINK), ("Overdrive", YELLOW),
    ("TurboCharge", GREEN), ("Kickback", YELLOW), ("ShootAgain", CREAM), ("RollCall1", CREAM), ("RollCall2", CREAM),
    ("RollCall3", CREAM), ("WingW", AQUA), ("WingI", AQUA), ("WingN", AQUA), ("WingG", AQUA),
]
# Where each shot arrow points (playfield units): the renderer rotates insert_arrow.png from the lamp to this.
# Revision 3: the Briefing arrow points up its channel (which leans right), the Turbine arrow straight up the
# Afterburner lane.
LAMP_AIM = {"ArrowCircuitLeft": (118.0, 440.0), "ArrowRunway": (192.0, 425.0), "ArrowHangar": (279.0, 392.0),
            "ArrowBriefing": (441.0, 400.0), "ArrowTurbine": (356.0, 415.0), "ArrowSkyway": (519.0, 425.0),
            "ArrowCircuitRight": (593.0, 440.0)}
INSERT_ARROW_UNITS = 36.0     # drawn size of insert_arrow.png (the arrow itself is 28 long with its bezel)
INSERT_ARROW_BRIEFING = 1.2   # the Briefing arrow is the largest on the table
INSERT_ROUND_PER_DIAMETER = 24.0 / 22.0   # insert_round.png is drawn this many times the lamp's diameter
# ---- revision 3: the new inserts and the flasher domes (TABLE_REV3.md 3.10) ----------------------------------
# TableLayout.hpp does not have these lamps yet (task 2e grows LampId and adds FlasherId): until it does, these
# positions are the art's proposal and the one place they are written. They are baked into the background
# unlit and exported to ArtPalette.hpp (kPlannedLamps, kFlasherDomes) so the code can take exactly these
# positions, or move them and run make_all.py again. Each was placed clear of every solid of the rev3 layout.
# Shapes: "round" (insert_round.png), "hex" (insert_hex.png), "chevron" (insert_chevron.png, points up).
BLUE_NEON = "#3CC8FF"
LADDER_DIAMETER = 12.0        # the small round inserts of the ladders (course lamps are 22, status 20)
HEX_DIAMETER = 11.0           # drone armour, corner to corner
CHEVRON_SIZE = (18.0, 9.0)    # rank chevrons, width and height
PLANNED_LAMPS = (
    # Drone armour, 1 (bottom) to 5: up the middle of the Afterburner lane, between the spinner and the gate.
    [("DroneArmour%d" % (i + 1), BLUE, "hex", (356.0, 386.0 - 12.5 * i)) for i in range(5)] +
    # G-meter 1G (bottom) to 5G: beside the G-force post, between the Runway's rail and the Hangar; it warms
    # from green to pink as the G rises.
    [("GForce%d" % (i + 1), c, "round", (212.0, 370.0 - 14.0 * i)) for i, c in enumerate((GREEN, GREEN, YELLOW, ORANGE, PINK))] +
    # T-A-X-I: one lamp on the field side of each stand-up.
    [("TaxiT", YELLOW, "round", (176.0, 462.0)), ("TaxiA", YELLOW, "round", (535.0, 462.0)),
     ("TaxiX", YELLOW, "round", (221.0, 392.0)), ("TaxiI", YELLOW, "round", (487.0, 450.0))] +
    # Rank ladder, 1 (bottom) to 7: concept A moves it out of the centre (the pylon, F-L-A-P-S and the Air
    # Pocket are there) to x 500, the mirror of the Altitude ladder's x 211.
    [("Rank%d" % (i + 1), ORCHID, "chevron", (500.0, 662.0 - 12.0 * i)) for i in range(7)] +
    # Combo 2X..5X left of centre and bonus multiplier 2X..5X mirrored on the right, rising toward the middle;
    # concept A: 16 lower, clear of the F and S rollovers.
    [("Combo%dX" % (i + 2), AQUA, "round", (262.0 + 16.0 * i, 664.0 - 6.0 * i)) for i in range(4)] +
    [("Bonus%dX" % (i + 2), PINK, "round", (449.0 - 16.0 * i, 664.0 - 6.0 * i)) for i in range(4)]
)
# Flasher domes: each stands on a solid (a wall end, the Hangar's roof, a slingshot's plastic), never where a
# ball can roll. The base is baked into the background with the dome unlit; the game draws flasher_dome.png
# tinted, and its flare over it, when it fires. Revision 3 has no upper flipper, so no Tower dome or arrow.
FLASHER_DOME_UNITS = 16.0     # drawn size of flasher_dome.png
FLASHER_DOMES = [
    ("Hangar", ORANGE, (279.0, 296.0)), ("Briefing", YELLOW, (473.0, 357.0)), ("Afterburner", GREEN, (391.0, 360.0)),
    ("Runway", ORCHID, (145.0, 392.0)), ("Skyway", PINK, (566.0, 392.0)), ("Nest", CREAM, (324.0, 168.0)),
    ("SlingLeft", AQUA, (150.0, 692.0)), ("SlingRight", AQUA, (561.0, 692.0)),
]
DRONE_SPRITE_UNITS = 44.0     # drone*.png cover this; the hexagon is 2 * the drone's radius corner to corner
CAPTIVE_BALL_SPRITE_UNITS = 30.0   # captive_ball.png; the ball is 22 of it (TABLE_REV3 3.5: radius 11)
STANDUP_SPRITE_UNITS = (14.0, 30.0)   # standup_*.png, drawn upright, rotated by the stand-up's angle minus 90

# Buttons take ink captions, so every face is light enough for 3:1 against INK, pressed (level 0.8) too:
# ORCHID, not VIOLET, for the nudge and menu keys; a light grey-violet for a key that is off or leads nowhere.
TINT = {"flipper": PINK, "bumper": ORANGE, "bumper_flash": YELLOW, "turbo_target": GREEN, "door": CH_MID,
        "door_marked": ORANGE, "button_centre": TEAL, "button_nudge": ORCHID, "button_pause": PINK,
        "button_launch": YELLOW, "button_off": "#8C84AA", "pad": AQUA}

# ---- visibility rule (see STATUS.md, review/visibility.png) ------------------
# Relative luminance (linear, 0..1) ceilings and floors the art and the renderer keep to.
LUM_DECK_MAX = 0.060      # any deck or floor-art pixel under a ball path, before lamps
LUM_STATIC_MAX = 0.45     # baked highlights and flares on static features
LUM_UNLIT_LENS = 0.10     # unlit insert lens, mean (at least 2:1 against the deck)
LUM_LIT_LENS_MAX = 0.75   # lit insert plus its glow, mean over the lens: always under the ball
LUM_BALL_MIN = 0.70       # ball disc, mean
UNLIT_LEVEL = 0.40        # tint multiplier for an unlit insert
GLOW_STRENGTH_MAX = 0.45  # strongest additive glow.png alpha the renderer uses on the playfield
GLOW_STRENGTH_LAMP = 0.30  # glow.png alpha for a lit lamp
GLOW_RADIUS_ARROW = 0.62   # glow radius as a share of the arrow sprite's drawn size
GLOW_RADIUS_ROUND = 1.10   # glow radius as a multiple of a round lamp's diameter
BALL_SPRITE_UNITS = 32.0  # ball.png covers this many units; the chrome disc is 24 of them
BALL_SHADOW_OFFSET = (4.0, 6.0)
# The ball's contact shadow (glow.png in INK) lies on the playfield, under every element: soft and small, never a
# dark smudge over a bumper or a slingshot.
BALL_SHADOW_ALPHA = 0.50
BALL_SHADOW_RADII = 1.3   # its radius, in ball radii


LUM_LIT_LENS = 0.42   # a lit lens is tinted no lighter than this, whatever its hue

# ---- light show (owner update: a lit, living machine; src/core/LightShow.*, src/platform/TableEffects.*) ----
# A lit lamp, a flash or a flasher is drawn in the neon of its colour: the same hue as the paint, brighter and
# more saturated, so lit against unlit is obvious. Neon lenses may be lighter than LUM_LIT_LENS but never reach
# the ball's LUM_BALL_MIN.
NEON = {AQUA: "#1EEBFF", ORCHID: "#C45CFF", ORANGE: "#FF8A00", YELLOW: "#FFEA28", GREEN: "#9DFF1E", PINK: "#FF2EC8",
        CREAM: "#FFF2B0", BLUE: BLUE_NEON}
# WCAG 2.3.1's red flash counts a colour as saturated red when R / (R + G + B), on channels linearised as for
# relative luminance, is 0.8 or more. Linearised, ORANGE's neon (#FF8A00) is 0.797, just under; PINK's (#FF2EC8)
# 0.62; the GI's amber walls (#FF9A3C) 0.73. The slingshots' GI was #FF7A1E (0.83, saturated red) and is nudged
# toward yellow, #FF8C28 (0.78). Blends over the floor can still come out saturated red here and there, so
# pinball_fxreel --wcag measures the red flash against the same fields and gates it like the general one.
LUM_NEON_LENS = 0.45
# The effect colours the renderer uses for bursts, sweeps and the general illumination.
FX = {"cyan": "#1EEBFF", "magenta": "#FF2EC8", "lime": "#9DFF1E", "orange": "#FF8A00", "yellow": "#FFEA28",
      "violet": "#C45CFF", "white": "#FFF8EC", "gi": "#FFB45C", "gi_bulb": "#FFD9A0", "trail": "#9FF4FF"}
GLOW_STRENGTH_NEON = 0.95      # glow.png alpha for a lit lamp at full level
FLASHER_STRENGTH_MAX = 0.95    # strongest flasher burst (flare.png / glow.png); the ball is drawn after it, with its rim
# The general illumination's colours (gi.png): amber walls, orange slingshots, the bumpers in turn.
GI_COLOURS = {"walls": "#FF9A3C", "slings": "#FF8C28", "bumpers": ["#FF3CC8", "#3CE6FF", "#FF3CC8"]}
GI_STRENGTH = 0.85             # gi.png, drawn additively in its own colours, at full illumination, at FL100
GI_STRENGTH_TOP = 1.00         # and at FL500 (the Altitude's top level): brighter as the game climbs
# The GI bulbs round the rim take the Altitude's colour (core/Progression.hpp, LightShow::giColour): day blue,
# clear white, dusk amber, sunset, night violet. Sunset is a coral, not a saturated red (R / (R + G + B) on
# linearised channels 0.74, under WCAG 2.3.1's 0.8), and the colour only ever turns slowly.
ALTITUDE_GI = ["#6FB4FF", "#F2F4FF", "#FFB050", "#FF8A5C", "#9A6BFF"]
GI_BULBS = 40                  # bulbs round the rim
GI_BULB_STRENGTH = 0.30        # glow.png alpha of each at FL100; GI_BULB_STRENGTH_TOP at FL500
GI_BULB_STRENGTH_TOP = 0.45
POOL = "#04040F"                # the pool's colour: darker than INK, so it is never taken for lettering
# The dark pool (pool.png) round each ball under the light show, on the playfield and the lamps but under every
# element (bumpers, slingshots, targets, posts): only as deep as the light round the ball calls for.
BALL_POOL_ALPHA = 0.50
BALL_POOL_RADII = 2.2         # its radius, in ball radii
FLARE_UNITS = 64.0             # flare.png and ring.png cover this many units at scale 1


def neon_level(h):
    """Colour multiplier for a lit neon lens: 1.0 unless the neon is lighter than LUM_NEON_LENS."""
    return round(min(1.0, (LUM_NEON_LENS / _lum(h)) ** (1 / 2.2)), 2)


def _lum(h):
    c = [int(h.lstrip("#")[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    c = [v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4 for v in c]
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def lit_level(h):
    """Colour multiplier for a lit insert of this colour: 1.0 unless the hue is lighter than LUM_LIT_LENS."""
    return round(min(1.0, (LUM_LIT_LENS / _lum(h)) ** (1 / 2.2)), 2)


def rgb(h):
    h = h.lstrip("#")
    return int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)


def mix_teal(row):
    """Teal of the mirrored floor squares on a chrome ball, dimmer toward the bottom of the ball."""
    r, g, b = rgb("#27D6CC")
    k = max(0.35, 1.0 - 0.12 * row)
    return "#%02x%02x%02x" % (int(r * k), int(g * k), int(b * k))
