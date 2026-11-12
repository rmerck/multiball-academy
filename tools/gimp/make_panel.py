# Generates assets/ui/panel.png: the information panel beside the playfield.
#
#   build/pinball_layout > /tmp/layout.json
#   REPO = "/path/to/repo"; LAYOUT = "/tmp/layout.json"
#   exec(open(REPO + "/tools/gimp/make_panel.py").read())
#
# The recessed display windows come from src/core/PanelLayout.hpp via the
# layout dump, so the art always lines up with the text drawn over it.
import json

exec(open(REPO + "/tools/gimp/common.py").read())

S = 2
REPLACE = Gimp.ChannelOps.REPLACE
ADD = Gimp.ChannelOps.ADD


def make_panel(repo, layout_path):
    with open(layout_path) as f:
        p = json.load(f)["panel"]
    left = p["left"]
    W, H = int(p["width"] * S), int(p["height"] * S)
    img = Gimp.Image.new(W, H, Gimp.ImageBaseType.RGB)
    Gimp.context_set_antialias(True)
    Gimp.context_set_feather(False)

    # Brushed dark metal.
    metal = new_layer(img, "metal")
    img.select_rectangle(REPLACE, 0, 0, W, H)
    linear_gradient(metal, color(0.13, 0.15, 0.22), color(0.05, 0.06, 0.10), 0, 0, W, H)
    select_none(img)
    apply_filter(metal, "gegl:noise-hurl", pct_random=10.0, repeat=1, seed=11)
    apply_filter(metal, "gegl:gaussian-blur", std_dev_x=9.0, std_dev_y=0.6)

    # A soft glow behind the title.
    title = new_layer(img, "title-glow", opacity=45, mode=Gimp.LayerMode.SCREEN)
    Gimp.context_set_feather(True)
    Gimp.context_set_feather_radius(90.0, 90.0)
    img.select_ellipse(REPLACE, 60 * S, 10 * S, (p["width"] - 120) * S, 130 * S)
    Gimp.context_set_feather(False)
    fill_selection(title, color(0.15, 0.55, 0.85))

    # Display windows: a bright bevel, then a dark glass inset.
    bevel = new_layer(img, "bevel")
    glass = new_layer(img, "glass")
    for x, y, w, h in p["wells"]:
        x -= left
        img.select_round_rectangle(REPLACE, (x - 3) * S, (y - 3) * S, (w + 6) * S, (h + 6) * S,
                                   9 * S, 9 * S)
        linear_gradient(bevel, color(0.04, 0.05, 0.08), color(0.42, 0.50, 0.66),
                        0, (y - 3) * S, 0, (y + h + 3) * S)
        img.select_round_rectangle(REPLACE, x * S, y * S, w * S, h * S, 7 * S, 7 * S)
        linear_gradient(glass, color(0.015, 0.02, 0.05), color(0.04, 0.06, 0.12),
                        0, y * S, 0, (y + h) * S)
    select_none(img)

    # Corner screws.
    screws = new_layer(img, "screws")
    for sx, sy in ((14, 14), (p["width"] - 14, 14), (14, p["height"] - 14),
                   (p["width"] - 14, p["height"] - 14)):
        select_circle(img, (sx, sy), 6, S)
        radial_gradient(screws, color(0.75, 0.78, 0.86), color(0.20, 0.22, 0.30),
                        (sx - 2) * S, (sy - 2) * S, (sx + 6) * S, (sy + 6) * S)
        img.select_rectangle(REPLACE, (sx - 4) * S, (sy - 0.8) * S, 8 * S, 1.6 * S)
        fill_selection(screws, color(0.10, 0.11, 0.16))
    select_none(img)

    # A dark seam where the panel meets the playfield.
    seam = new_layer(img, "seam")
    img.select_rectangle(REPLACE, 0, 0, 5 * S, H)
    linear_gradient(seam, color(0.0, 0.0, 0.0, 1.0), color(0.0, 0.0, 0.0, 0.0), 0, 0, 5 * S, 0)
    select_none(img)

    img.flatten()
    path = export_png(img, repo + "/assets/ui/panel.png")
    img.delete()
    return path


print(make_panel(REPO, LAYOUT))
