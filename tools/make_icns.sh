#!/bin/sh
# Builds packaging/macos/AppIcon.icns from packaging/macos/icon_1024.png
# (drawn by tools/gimp/make_icon.py) with the system's sips and iconutil:
# every standard size from 16 to 512 points, each at 1x and 2x.
#
# Usage: tools/make_icns.sh [source.png [output.icns]]
# macOS only. The result is committed, so a build does not need this script.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "$here")
source_png=${1:-"$repo/packaging/macos/icon_1024.png"}
output=${2:-"$repo/packaging/macos/AppIcon.icns"}

fail() {
  echo "make_icns: $*" >&2
  exit 1
}

[ "$#" -le 2 ] || fail "usage: $0 [source.png [output.icns]]"
command -v sips >/dev/null 2>&1 || fail "sips not found (macOS only)"
command -v iconutil >/dev/null 2>&1 || fail "iconutil not found (macOS only)"
[ -f "$source_png" ] || fail "no such file: $source_png"

width=$(sips -g pixelWidth "$source_png" | awk '/pixelWidth/ {print $2}')
height=$(sips -g pixelHeight "$source_png" | awk '/pixelHeight/ {print $2}')
[ "$width" = 1024 ] && [ "$height" = 1024 ] \
  || fail "$source_png is ${width}x${height}, expected 1024x1024"

work=$(mktemp -d "${TMPDIR:-/tmp}/make_icns.XXXXXX")
trap 'rm -rf "$work"' EXIT
iconset="$work/AppIcon.iconset"
mkdir "$iconset"

# name:pixels
for entry in \
  16x16:16 16x16@2x:32 \
  32x32:32 32x32@2x:64 \
  128x128:128 128x128@2x:256 \
  256x256:256 256x256@2x:512 \
  512x512:512 512x512@2x:1024
do
  name=${entry%%:*}
  pixels=${entry##*:}
  sips -s format png -z "$pixels" "$pixels" "$source_png" \
    --out "$iconset/icon_$name.png" >/dev/null
done

iconutil --convert icns --output "$work/AppIcon.icns" "$iconset"
mv -f "$work/AppIcon.icns" "$output"
echo "$output"
