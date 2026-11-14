# Application icons. No text, no layout file needed.
#   python3 make_icons.py [root]     writes packaging/ios/Assets.xcassets/AppIcon.appiconset/icon_1024.png (opaque,
#   full bleed, RGB) and packaging/macos/icon_1024.png (824 px rounded square on transparency, RGBA).
import math
import os
import sys

import numpy as np
import skia
from PIL import Image

import palette as P
from common import C, ROOT, Stage, blur, fill, hexrgb, lin, plus, poly_path, rad, ramp, rrect, save_png, shade, stroke

HORIZON = 0.56


def sphere(c, x, y, r, tint=None):
    """A ray-traced chrome ball: sky above, the chequered floor below, one hard light."""
    c.drawOval(skia.Rect.MakeLTRB(x - r * 0.95, y + r * 0.74, x + r * 1.55, y + r * 1.2), blur(fill(C("#02010A", 215)), r * 0.13))
    c.drawCircle(x, y, r * 1.012, fill(C(P.INK)))
    c.save()
    cp = skia.Path(); cp.addCircle(x, y, r * 0.99); c.clipPath(cp, skia.ClipOp.kIntersect, True)
    # Sky in the upper half: night at the top, the glow of the horizon at the equator.
    c.drawRect(skia.Rect.MakeLTRB(x - r, y - r, x + r, y + r * 0.14),
               shade(lin((x, y - r), (x, y + r * 0.14), [C("#0B0838"), C("#2A1E8C"), C("#7C55E0"), C("#F1E4FF")], [0, 0.45, 0.82, 1])))
    # The floor in the lower half, bent round the ball.
    c.drawRect(skia.Rect.MakeLTRB(x - r, y + r * 0.14, x + r, y + r), fill(C("#0A0824")))
    for j in range(6):
        y0 = y + r * (0.14 + 0.15 * j); y1 = y + r * (0.14 + 0.15 * (j + 1))
        for i in range(-10, 10):
            if (i + j) % 2 == 0:
                w0 = r * (0.34 - 0.04 * j); w1 = r * (0.34 - 0.04 * (j + 1))
                c.drawPath(poly_path([(x + i * w0, y0), (x + (i + 1) * w0, y0), (x + (i + 1) * w1, y1), (x + i * w1, y1)]),
                           fill(C(P.mix_teal(j))))
    # A soft light across the upper left, the dark limb, and a pink return light low on the right.
    c.drawCircle(x - r * 0.3, y - r * 0.36, r * 0.62, shade(rad((x - r * 0.3, y - r * 0.36), r * 0.62, [C(P.WHITE, 150), C(P.WHITE, 0)])))
    c.drawCircle(x, y, r, shade(rad((x, y), r, [C("#000000", 0), C("#000000", 0), C("#05031A", 215)], [0, 0.80, 1])))
    top = skia.Path(); top.addArc(skia.Rect.MakeLTRB(x - r * 0.975, y - r * 0.975, x + r * 0.975, y + r * 0.975), 180, 180)
    c.drawPath(top, blur(stroke(C("#CDBBFF", 150), r * 0.022), r * 0.012))
    arc = skia.Path(); arc.addArc(skia.Rect.MakeLTRB(x - r * 0.93, y - r * 0.93, x + r * 0.93, y + r * 0.93), 15, 70)
    c.drawPath(arc, blur(stroke(C("#FF7AC0", 170), r * 0.045), r * 0.03))
    c.restore()
    hx, hy = x - r * 0.36, y - r * 0.44
    c.drawOval(skia.Rect.MakeLTRB(hx - r * 0.2, hy - r * 0.13, hx + r * 0.2, hy + r * 0.13), blur(fill(C(P.WHITE)), r * 0.03))
    return hx, hy


def scene(size):
    """The icon picture on a square of `size` pixels."""
    st = Stage(size, size, 1, 2)
    xs, ys = st.grid()
    hz = size * HORIZON
    sky = ramp([(0, "#04031A"), (0.55, "#17105A"), (0.9, "#6A35C9"), (1, "#C79BFF")], ys / hz)
    z = (size * 0.075) / np.maximum(ys - hz, 0.5)
    u = (xs - size * 0.5) * z / (size * 0.036)
    chk = (np.floor(u) + np.floor(z * 3.0 + 0.35)) % 2
    fog = np.clip((ys - hz) / (size * 0.16), 0, 1)
    floor = ramp([(0, "#0A0824"), (1, "#19B7B0")], chk) * fog[..., None] + np.array(hexrgb("#8A5AE0"), np.float32) * (1 - fog[..., None])
    floor *= (0.5 + 0.5 * np.exp(-((xs - size * 0.5) / (size * 0.5)) ** 2))[..., None]
    st.put(np.where((ys < hz)[..., None], sky, floor))
    c = st.reopen()
    rng = np.random.default_rng(27)
    for _ in range(70):
        c.drawCircle(rng.uniform(0, size), rng.uniform(0, hz * 0.7), rng.uniform(0.0008, 0.0024) * size, fill(C(P.CREAM, int(rng.integers(90, 230)))))
    s = size
    sphere(c, s * 0.80, s * 0.62, s * 0.085)
    sphere(c, s * 0.215, s * 0.665, s * 0.135)
    hx, hy = sphere(c, s * 0.53, s * 0.50, s * 0.285)
    for ang, Ln, wd in ((0, 0.22, 0.005), (90, 0.10, 0.0045), (45, 0.045, 0.003), (135, 0.045, 0.003)):
        a = math.radians(ang)
        c.drawLine(hx - Ln * s * math.cos(a), hy - Ln * s * math.sin(a), hx + Ln * s * math.cos(a), hy + Ln * s * math.sin(a),
                   plus(blur(stroke(C(P.WHITE, 225), wd * s), 0.003 * s)))
    c.drawCircle(hx, hy, s * 0.07, plus(shade(rad((hx, hy), s * 0.07, [C(P.WHITE, 200), C("#9FD8FF", 50), C("#9FD8FF", 0)], [0, 0.3, 1]))))
    for (k, r, col, al) in ((0.42, 0.028, P.AQUA, 60), (0.68, 0.05, "#FF8FC0", 40), (0.9, 0.018, P.YELLOW, 70)):
        c.drawCircle(hx + (s * 0.86 - hx) * k, hy + (s * 0.88 - hy) * k, r * s, plus(fill(C(col, al))))
    return st.image()


def ios():
    return scene(1024).convert("RGB")


def macos():
    inner = scene(824).convert("RGBA")
    mask = Stage(824, 824, 1, 2, transparent=True)
    mask.c.drawRRect(rrect(0, 0, 824, 824, 185.4), fill(C(P.WHITE)))
    m = mask.image().split()[3]
    out = Stage(1024, 1024, 1, 2, transparent=True)
    out.c.drawRRect(rrect(100, 110, 824, 824, 185.4), blur(fill(C("#000000", 90)), 11))
    base = out.image()
    base.paste(inner, (100, 100), m)
    return base


def main(root):
    save_png(ios(), os.path.join(root, "packaging", "ios", "Assets.xcassets", "AppIcon.appiconset", "icon_1024.png"))
    save_png(macos(), os.path.join(root, "packaging", "macos", "icon_1024.png"))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ROOT)
