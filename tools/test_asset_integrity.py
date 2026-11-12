#!/usr/bin/env python3
"""End-to-end check that the game refuses to start with altered assets.

Copies the built executable and its assets to a scratch directory, confirms
it runs, then damages the assets in several ways and confirms each run is
refused with exit code 1 and an integrity message.

Usage: test_asset_integrity.py <pinball-executable> <scratch-dir>
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path


def run(exe: Path):
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    # The short timeout turns a hang into a failure. Output is decoded
    # leniently: a crash may print anything.
    try:
        result = subprocess.run([str(exe), "--frames", "40"], env=env, capture_output=True,
                                text=True, errors="replace", timeout=20)
    except subprocess.TimeoutExpired:
        return None, "no exit within 20 seconds"
    return result.returncode, result.stdout + result.stderr


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    source_exe = Path(sys.argv[1]).resolve()
    scratch = Path(sys.argv[2]).resolve()
    source_assets = source_exe.parent / "assets"
    if not source_exe.is_file() or not source_assets.is_dir():
        print("FAIL: %s or the assets directory next to it is missing" % source_exe)
        return 1

    # Only ever remove a directory this script created.
    marker = scratch / ".asset_integrity_scratch"
    if scratch.exists():
        if not marker.exists():
            print("FAIL: %s exists and is not a scratch directory" % scratch)
            return 1
        shutil.rmtree(scratch)
    scratch.mkdir(parents=True)
    marker.touch()

    exe = scratch / source_exe.name
    shutil.copy2(source_exe, exe)
    assets = scratch / "assets"

    def fresh_assets():
        if assets.exists():
            shutil.rmtree(assets)
        shutil.copytree(source_assets, assets)
        return sorted(p for p in assets.rglob("*.png"))

    failures = []

    def expect(label, want_ok):
        code, output = run(exe)
        if want_ok:
            if code != 0:
                failures.append("%s: expected a clean run, got exit %s\n%s" % (label, code, output))
        else:
            if code != 1:
                failures.append("%s: expected exit 1, got %s\n%s" % (label, code, output))
            elif "Asset " not in output:
                failures.append("%s: refused without an asset error message\n%s" % (label, output))
            if "Sanitizer" in output or "runtime error" in output:
                failures.append("%s: sanitizer report\n%s" % (label, output))

    files = fresh_assets()
    if not files:
        print("FAIL: no assets next to %s" % source_exe)
        return 1
    expect("intact", True)

    for target in files:
        rel = target.relative_to(assets)

        fresh_assets()
        data = bytearray(target.read_bytes())
        data[len(data) // 2] ^= 0x01
        target.write_bytes(bytes(data))
        expect("%s one bit flipped" % rel, False)

        fresh_assets()
        with open(target, "ab") as f:
            f.write(b"\x00")
        expect("%s one byte appended" % rel, False)

        fresh_assets()
        data = target.read_bytes()
        target.write_bytes(data[:-1])
        expect("%s truncated" % rel, False)

        fresh_assets()
        target.write_bytes(b"")
        expect("%s emptied" % rel, False)

        fresh_assets()
        target.unlink()
        expect("%s deleted" % rel, False)

        # A FIFO or a directory in place of the file must be refused at once,
        # not opened (opening a FIFO with no writer would block forever).
        if hasattr(os, "mkfifo"):
            fresh_assets()
            target.unlink()
            os.mkfifo(target)
            expect("%s replaced by a FIFO" % rel, False)

        # A symbolic link is refused even when it points at the right bytes.
        if os.name == "posix":
            fresh_assets()
            genuine = target.with_name(target.name + ".real")
            target.rename(genuine)
            os.symlink(genuine, target)
            expect("%s replaced by a link to a genuine copy" % rel, False)

        fresh_assets()
        target.unlink()
        target.mkdir()
        expect("%s replaced by a directory" % rel, False)

    # An extra file that is not in the manifest is simply never opened.
    fresh_assets()
    (assets / "sprites").mkdir(exist_ok=True)
    (assets / "sprites" / "unlisted.png").write_bytes(b"not a png")
    expect("unlisted extra file", True)

    fresh_assets()
    expect("restored", True)

    shutil.rmtree(scratch)
    if failures:
        for f in failures:
            print("FAIL " + f)
        return 1
    print("asset integrity: %d file(s), all tampering refused" % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
