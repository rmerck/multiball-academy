#!/usr/bin/env python3
"""Checks the "screen" part of the pinball_layout dump: the art scripts read
it, so it must be valid JSON and agree with itself.

Usage: check_layout_json.py <path to pinball_layout>
"""
import json
import subprocess
import sys


def inside(inner, outer, slack=1.0):
    return (inner[0] >= outer[0] - slack and inner[1] >= outer[1] - slack
            and inner[0] + inner[2] <= outer[0] + outer[2] + slack
            and inner[1] + inner[3] <= outer[1] + outer[3] + slack)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    dump = json.loads(subprocess.check_output([sys.argv[1]], timeout=20))
    screen = dump["screen"]
    assert screen["appName"].strip() and screen["appName"] == screen["appName"].strip()
    assert screen["playfield"] == [dump["width"], dump["height"]]
    assert len(screen["backdropRgb"]) == 3
    strip, deck = screen["strip"], screen["deck"]
    stack = screen["stack"]
    for name, well in strip.items():
        assert inside(well, [0, 0, stack["width"], stack["stripHeight"]], 0), name
    for name in ("nudgeLeftButton", "centreButton", "nudgeRightButton", "leftCorner",
                 "rightCorner"):
        assert inside(deck[name], [0, 0, stack["width"], stack["deckMinHeight"]], 0), name
    panel = [dump["panel"]["left"], 0, dump["panel"]["width"], dump["panel"]["height"]]
    for name, area in screen["panelAreas"].items():
        assert inside(area, panel, 0), name

    names = [p["name"] for p in screen["presets"]]
    assert len(names) == len(set(names)) and "desktop" in names and len(names) >= 10
    least = screen["minHitUnits"]
    for p in screen["presets"]:
        canvas = [0, 0] + p["canvas"]
        assert p["playfield"][2:] == screen["playfield"], p["name"]
        assert p["composition"] in ("classic", "stack") and p["idiom"] in ("desktop", "touch")
        for key in ("safe", "playfield", "hud", "deck", "dialog", "flipperLeftZone",
                    "flipperRightZone"):
            assert inside(p[key], canvas), (p["name"], key)
        for part in ("menu", "about", "initials"):
            assert inside(p[part]["box"], p["dialog"]), (p["name"], part)
        buttons = [p[k] for k in ("pauseButton", "nudgeLeftButton", "centreButton",
                                  "nudgeRightButton")]
        if p["idiom"] == "desktop":
            assert p["canvas"] == screen["classic"] and all(b[2] == 0 for b in buttons)
            continue
        surface = p["surface"]
        assert abs(p["canvas"][0] * p["pointsPerUnit"] - surface["width"]) < 0.05, p["name"]
        assert abs(p["canvas"][1] * p["pointsPerUnit"] - surface["height"]) < 0.05, p["name"]
        for b in buttons:
            assert inside(b, p["safe"]) and min(b[2], b[3]) >= least, p["name"]
            assert min(b[2], b[3]) * p["pointsPerUnit"] >= screen["minHitPoints"], p["name"]
    print("screen layouts:", ", ".join(names))


if __name__ == "__main__":
    main()
