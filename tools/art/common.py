# Shared drawing helpers for the art generators (skia + numpy, fixed seeds only).
import json
import math
import os

import numpy as np
import skia
from PIL import Image, ImageFilter

import palette as P

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
FONTS = os.path.join(HERE, "sources", "fonts")
LAYOUT_PATH = os.environ.get("PINBALL_LAYOUT", os.path.join(ROOT, "build", "layout.json"))


def layout():
    if not os.path.isfile(LAYOUT_PATH):
        raise SystemExit("layout file missing: %s (run build/pinball_layout > build/layout.json)" % LAYOUT_PATH)
    with open(LAYOUT_PATH) as f:
        return json.load(f)


hexrgb = P.rgb


def C(h, a=255):
    r, g, b = hexrgb(h)
    return skia.Color(r, g, b, int(a))


def mix(h1, h2, t):
    a, b = hexrgb(h1), hexrgb(h2)
    return "#%02x%02x%02x" % tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def font(name, size):
    tf = skia.Typeface.MakeFromFile(os.path.join(FONTS, name))
    if tf is None:
        raise SystemExit("font missing: " + name)
    f = skia.Font(tf, size)
    f.setSubpixel(True)
    f.setEdging(skia.Font.Edging.kAntiAlias)
    f.setHinting(skia.FontHinting.kNone)
    return f


def text_path(text, fnt, x, y, tracking=0.0, align="left"):
    glyphs = fnt.textToGlyphs(text)
    widths = fnt.getWidths(glyphs)
    total = sum(widths) + tracking * (len(glyphs) - 1)
    if align == "center":
        x -= total / 2
    elif align == "right":
        x -= total
    out = skia.Path()
    for g, w in zip(glyphs, widths):
        p = fnt.getPath(g)
        if p is not None:
            p.offset(x, y)
            out.addPath(p)
        x += w + tracking
    return out, total


def fit_font(name, text, width, tracking=0.0, start=160.0):
    size = start
    while True:
        f = font(name, size)
        _, w = text_path(text, f, 0, 0, tracking)
        if w <= width:
            return f
        size -= 1.0


def poly_path(pts, close=True):
    p = skia.Path()
    p.moveTo(*pts[0])
    for q in pts[1:]:
        p.lineTo(*q)
    if close:
        p.close()
    return p


def rrect(x, y, w, h, r):
    return skia.RRect.MakeRectXY(skia.Rect.MakeXYWH(x, y, w, h), r, r)


def fill(color, **kw):
    return skia.Paint(AntiAlias=True, Color=color, Style=skia.Paint.kFill_Style, **kw)


def shade(shader, **kw):
    return skia.Paint(AntiAlias=True, Shader=shader, **kw)


def stroke(color, w, cap=skia.Paint.kRound_Cap, join=skia.Paint.kRound_Join, **kw):
    return skia.Paint(AntiAlias=True, Color=color, Style=skia.Paint.kStroke_Style, StrokeWidth=w,
                      StrokeCap=cap, StrokeJoin=join, **kw)


def blur(paint, sigma):
    paint.setMaskFilter(skia.MaskFilter.MakeBlur(skia.kNormal_BlurStyle, sigma))
    return paint


def plus(paint):
    paint.setBlendMode(skia.BlendMode.kPlus)
    return paint


def lin(p0, p1, cols, pos=None):
    return skia.GradientShader.MakeLinear(points=[p0, p1], colors=cols, positions=pos)


def rad(c, r, cols, pos=None):
    return skia.GradientShader.MakeRadial(center=c, radius=r, colors=cols, positions=pos)


def sweep(cx, cy, cols):
    return skia.GradientShader.MakeSweep(cx, cy, cols)


CHROME_SWEEP = None


def chrome_sweep(cx, cy):
    return sweep(cx, cy, [C(P.CH_LO), C(P.CH_HI), C(P.CH_MID), C(P.CH_LO), C(P.WHITE), C(P.CH_MID), C(P.CH_LO)])


class Stage:
    """A numpy RGBA canvas in logical units: `scale` output pixels per unit, drawn `ss` times larger."""

    def __init__(self, w_units, h_units, scale=2, ss=2, transparent=False, origin=(0.0, 0.0)):
        self.scale, self.ss, self.k = scale, ss, scale * ss
        self.wu, self.hu, self.origin = w_units, h_units, origin
        self.w = int(round(w_units * self.k))
        self.h = int(round(h_units * self.k))
        self.transparent = transparent
        self.arr = np.zeros((self.h, self.w, 4), np.uint8)
        if not transparent:
            self.arr[..., 3] = 255
        self.open()

    def open(self):
        info = skia.ImageInfo.Make(self.w, self.h, skia.kRGBA_8888_ColorType, skia.kPremul_AlphaType)
        self.surface = skia.Surface.MakeRasterDirect(info, self.arr)
        self.c = self.surface.getCanvas()
        self.c.scale(self.k, self.k)
        self.c.translate(-self.origin[0], -self.origin[1])

    def flush(self):
        self.surface.flushAndSubmit()

    def reopen(self):
        self.flush()
        self.open()
        return self.c

    def grid(self):
        ys, xs = np.mgrid[0:self.h, 0:self.w].astype(np.float32)
        return (xs + 0.5) / self.k + self.origin[0], (ys + 0.5) / self.k + self.origin[1]

    def put(self, rgb, clip=None, alpha=255):
        """Lay a device-sized RGB(A) float/uint8 array over the canvas (optionally clipped by a unit path)."""
        rgb = np.asarray(rgb)
        if rgb.shape[2] == 3:
            a = np.empty(rgb.shape[:2] + (4,), np.uint8)
            a[..., :3] = np.clip(rgb, 0, 255).astype(np.uint8)
            a[..., 3] = 255
        else:
            a = np.clip(rgb, 0, 255).astype(np.uint8)
        img = skia.Image.fromarray(np.ascontiguousarray(a), colorType=skia.kRGBA_8888_ColorType,
                                   alphaType=skia.kUnpremul_AlphaType)
        c = self.c
        c.save()
        if clip is not None:
            c.clipPath(clip, skia.ClipOp.kIntersect, True)
        c.resetMatrix()
        c.drawImage(img, 0, 0, skia.SamplingOptions(), skia.Paint(Alphaf=alpha / 255.0))
        c.restore()

    def image(self):
        """Finished image: RGBA (straight alpha) if transparent, else RGB, at `scale` px per unit."""
        self.flush()
        a = self.arr.astype(np.float32)
        if self.transparent:
            al = a[..., 3:4]
            rgbf = np.where(al > 0, a[..., :3] * 255.0 / np.maximum(al, 1), 0)
            a = np.concatenate([np.clip(rgbf, 0, 255), al], axis=2)
            im = Image.fromarray(a.astype(np.uint8), "RGBA")
            if self.ss != 1:
                # Resize in premultiplied space so edges do not pick up the colour of clear pixels.
                pm = Image.fromarray(self.arr.copy(), "RGBa").resize((self.w // self.ss, self.h // self.ss), Image.LANCZOS)
                im = pm.convert("RGBA")
            return im
        im = Image.fromarray(self.arr[..., :3].copy(), "RGB")
        if self.ss != 1:
            im = im.resize((self.w // self.ss, self.h // self.ss), Image.LANCZOS)
        return im


# ---- numpy textures -----------------------------------------------------------
def soft_noise(h, w, scale, seed):
    rng = np.random.default_rng(seed)
    small = rng.random((max(2, h // scale + 2), max(2, w // scale + 2))).astype(np.float32)
    im = Image.fromarray((small * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC)
    im = im.filter(ImageFilter.GaussianBlur(scale * 0.6))
    a = np.asarray(im, np.float32) / 255.0
    return (a - a.min()) / max(1e-6, float(a.max() - a.min()))


def ramp(stops, t):
    t = np.clip(t, 0, 1)
    pos = np.array([s[0] for s in stops], np.float32)
    cols = np.array([hexrgb(s[1]) for s in stops], np.float32)
    out = np.empty(t.shape + (3,), np.float32)
    for i in range(3):
        out[..., i] = np.interp(t, pos, cols[:, i])
    return out


BAYER4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], np.float32) / 16.0 - 0.5 + 1 / 32


def ordered_dither(im, levels=10, cell=2, strength=1.0):
    """Quantise an RGB image with a 4x4 Bayer matrix whose cells are `cell` pixels."""
    rgb = np.asarray(im.convert("RGB"))
    h, w = rgb.shape[:2]
    ys, xs = np.mgrid[0:h, 0:w]
    th = BAYER4[(ys // cell) % 4, (xs // cell) % 4]
    step = 255.0 / (levels - 1)
    v = rgb.astype(np.float32) + th[..., None] * step * strength
    return Image.fromarray((np.clip(np.round(v / step), 0, levels - 1) * step).astype(np.uint8), "RGB")


def deck_plates(xs, ys, tile=37.0, origin=(355.5, 300.0), spot=None, seed=1):
    """The lead surface: dark, cool, bevelled deck plates in two near tones. Returns float RGB.
    spot = (x, y, rx, ry) of the light pool, or None for an even surface."""
    fx = ((xs - origin[0]) / tile) % 1.0
    fy = ((ys - origin[1]) / tile) % 1.0
    chk = (np.floor((xs - origin[0]) / tile) + np.floor((ys - origin[1]) / tile)) % 2
    base = ramp([(0, P.DECK_A), (1, P.DECK_B)], chk)
    if spot is not None:
        s = np.exp(-(((xs - spot[0]) / spot[2]) ** 2 + ((ys - spot[1]) / spot[3]) ** 2))
        base = base * (0.72 + 0.28 * s)[..., None] + s[..., None] * np.array([6, 10, 22], np.float32)
    edge = 0.05
    hi = ((fx < edge) | (fy < edge)).astype(np.float32)
    lo = ((fx > 1 - edge) | (fy > 1 - edge)).astype(np.float32)
    base += hi[..., None] * np.array([12, 15, 26], np.float32) - lo[..., None] * 9
    # Rivets at the plate corners and a faint brushed grain: detail in the low-mid tones only.
    d = np.hypot(np.minimum(fx, 1 - fx), np.minimum(fy, 1 - fy)) * tile
    base += np.clip(1.0 - np.abs(d - 4.2) / 0.9, 0, 1)[..., None] * np.array([10, 13, 22], np.float32)
    rng = np.random.default_rng(seed)
    g = rng.normal(0, 1.0, (xs.shape[0], 1)).astype(np.float32) * 1.2
    return base + g[..., None]


def lum(rgb):
    """Relative luminance (linear) of an sRGB uint8/float array."""
    c = np.asarray(rgb, np.float32)[..., :3] / 255.0
    c = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    return 0.2126 * c[..., 0] + 0.7152 * c[..., 1] + 0.0722 * c[..., 2]


def save_png(im, path):
    """Write a PNG with only IHDR, IDAT and IEND: no text, time, gamma, profile or EXIF."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    mode = "RGBA" if im.mode in ("RGBA", "LA", "RGBa") else "RGB"
    clean = Image.new(mode, im.size)
    clean.paste(im.convert(mode))
    clean.save(path, "PNG", optimize=False, compress_level=9)
