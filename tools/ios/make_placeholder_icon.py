#!/usr/bin/env python3
"""Makes the TEMPORARY iOS application icon from the macOS one.

iOS wants one opaque, square, full-bleed 1024x1024 PNG with no alpha channel.
The macOS icon is a rounded square on transparency, so this lays it over the
game's backdrop colour and draws a striped frame round the edge to mark the
result as a stand-in. The real icon is drawn by the art scripts and replaces
packaging/ios/Assets.xcassets/AppIcon.appiconset/icon_1024.png; this script
and its output then go.

Usage: make_placeholder_icon.py [source.png [output.png]]
Standard library only. The result is committed, so a build does not run this.
"""
import struct
import sys
import zlib
from pathlib import Path

SIGNATURE = b"\x89PNG\r\n\x1a\n"
SIZE = 1024
# screen::kBackdropRgb in src/core/ScreenLayout.hpp.
BACKDROP = (10, 0, 26)
FRAME = 56
STRIPE = 64
STRIPE_COLOURS = ((250, 204, 21), (10, 0, 26))


def read_rgba(path):
    """Returns the rows of an 8-bit RGBA, non-interlaced PNG as bytearrays."""
    data = path.read_bytes()
    if not data.startswith(SIGNATURE):
        raise ValueError("not a PNG")
    pos = len(SIGNATURE)
    header = None
    packed = b""
    while pos + 12 <= len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if len(body) != length:
            raise ValueError("truncated chunk")
        if zlib.crc32(kind + body) & 0xFFFFFFFF != struct.unpack(
                ">I", data[pos + 8 + length:pos + 12 + length])[0]:
            raise ValueError("bad CRC")
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            packed += body
        elif kind == b"IEND":
            break
        pos += 12 + length
    if header != (SIZE, SIZE, 8, 6, 0, 0, 0):
        raise ValueError("expected %dx%d, 8-bit RGBA, not interlaced" % (SIZE, SIZE))
    raw = zlib.decompress(packed)
    stride = SIZE * 4
    if len(raw) != SIZE * (stride + 1):
        raise ValueError("wrong amount of pixel data")
    rows = []
    previous = bytearray(stride)
    for y in range(SIZE):
        start = y * (stride + 1)
        kind = raw[start]
        row = bytearray(raw[start + 1:start + 1 + stride])
        if kind == 1:
            for i in range(4, stride):
                row[i] = (row[i] + row[i - 4]) & 0xFF
        elif kind == 2:
            for i in range(stride):
                row[i] = (row[i] + previous[i]) & 0xFF
        elif kind == 3:
            for i in range(stride):
                left = row[i - 4] if i >= 4 else 0
                row[i] = (row[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif kind == 4:
            for i in range(stride):
                a = row[i - 4] if i >= 4 else 0
                b = previous[i]
                c = previous[i - 4] if i >= 4 else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                nearest = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                row[i] = (row[i] + nearest) & 0xFF
        elif kind != 0:
            raise ValueError("unknown filter")
        rows.append(row)
        previous = row
    return rows


def write_rgb(path, rows):
    """Writes 8-bit RGB with only the IHDR, IDAT and IEND chunks."""
    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body
                + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))
    raw = b"".join(b"\x00" + bytes(row) for row in rows)
    path.write_bytes(SIGNATURE
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9))
                     + chunk(b"IEND", b""))


def main(argv):
    repo = Path(__file__).resolve().parent.parent.parent
    if len(argv) > 3:
        print(__doc__, file=sys.stderr)
        return 1
    source = Path(argv[1]) if len(argv) > 1 else repo / "packaging/macos/icon_1024.png"
    output = Path(argv[2]) if len(argv) > 2 else (
        repo / "packaging/ios/Assets.xcassets/AppIcon.appiconset/icon_1024.png")
    rows = []
    for y, rgba in enumerate(read_rgba(source)):
        row = bytearray(SIZE * 3)
        for x in range(SIZE):
            if min(x, y, SIZE - 1 - x, SIZE - 1 - y) < FRAME:
                # The frame: diagonal stripes, so nobody ships this by mistake.
                colour = STRIPE_COLOURS[((x + y) // STRIPE) % 2]
            else:
                alpha = rgba[x * 4 + 3]
                colour = tuple((rgba[x * 4 + c] * alpha + BACKDROP[c] * (255 - alpha) + 127) // 255
                               for c in range(3))
            row[x * 3:x * 3 + 3] = bytes(colour)
        rows.append(row)
    write_rgb(output, rows)
    print("wrote %s" % output)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
