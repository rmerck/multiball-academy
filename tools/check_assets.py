#!/usr/bin/env python3
"""Verifies every PNG under assets/: valid structure, size limits, and no
metadata chunks (EXIF, text, timestamps, ICC profiles, thumbnails).

Usage: check_assets.py <assets-dir>
"""
import struct
import sys
import zlib
from pathlib import Path

SIGNATURE = b"\x89PNG\r\n\x1a\n"
# Pixel data and the chunks needed to interpret it. Everything else is refused.
ALLOWED = {b"IHDR", b"PLTE", b"tRNS", b"IDAT", b"IEND", b"sRGB", b"gAMA", b"cHRM", b"sBIT"}
MAX_EDGE = 4096
MAX_BYTES = 16 * 1024 * 1024


def check(path: Path) -> list:
    errors = []
    data = path.read_bytes()
    if len(data) > MAX_BYTES:
        errors.append("file larger than %d bytes" % MAX_BYTES)
    if not data.startswith(SIGNATURE):
        return errors + ["not a PNG"]
    pos = len(SIGNATURE)
    seen_end = False
    first = True
    while pos + 12 <= len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        crc_bytes = data[pos + 8 + length:pos + 12 + length]
        if len(body) != length or len(crc_bytes) != 4:
            errors.append("truncated chunk %r" % kind)
            break
        if zlib.crc32(kind + body) & 0xFFFFFFFF != struct.unpack(">I", crc_bytes)[0]:
            errors.append("bad CRC in chunk %r" % kind)
        if first:
            if kind != b"IHDR" or length != 13:
                errors.append("first chunk is not a 13-byte IHDR")
            else:
                width, height = struct.unpack(">II", body[:8])
                if not (0 < width <= MAX_EDGE and 0 < height <= MAX_EDGE):
                    errors.append("size %dx%d outside 1..%d" % (width, height, MAX_EDGE))
            first = False
        if kind not in ALLOWED:
            errors.append("metadata chunk %s is not allowed" % kind.decode("latin-1"))
        pos += 12 + length
        if kind == b"IEND":
            seen_end = True
            break
    if not seen_end:
        errors.append("missing IEND")
    elif pos != len(data):
        errors.append("%d bytes of trailing data after IEND" % (len(data) - pos))
    return errors


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    root = Path(sys.argv[1])
    # Hidden files (such as .DS_Store) are not tracked and cannot be loaded:
    # the game rejects any name component starting with a dot.
    files = sorted(f for f in root.rglob("*") if not f.name.startswith("."))
    pngs = [f for f in files if f.is_file() and f.suffix == ".png"]
    others = [f for f in files if f.is_file() and f.suffix != ".png"]
    failed = False
    for f in others:
        print("FAIL %s: only .png files belong in assets" % f.relative_to(root))
        failed = True
    for f in pngs:
        for e in check(f):
            print("FAIL %s: %s" % (f.relative_to(root), e))
            failed = True
    if not pngs:
        print("FAIL: no assets found")
        failed = True
    if not failed:
        print("check_assets: %d file(s) ok" % len(pngs))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
