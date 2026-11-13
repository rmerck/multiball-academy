#!/bin/bash
# Puts the application's debug symbols into an Xcode archive.
#
# Usage: collect_dsym.sh <Name.xcarchive> <iOS build directory>
#
# `xcodebuild archive` on the CMake-generated project leaves the .dSYM in
# the build directory (<build>/Release-iphoneos), not in the archive's dSYMs
# directory, where the export and App Store Connect look for it. This copies
# it there, after checking that it belongs to the archived executable: the
# two must carry the same UUID. Run it between the archive and the export
# (docs/BUILDING.md). macOS with Xcode only.
set -euo pipefail

fail() {
  echo "collect_dsym: $*" >&2
  exit 1
}

[ "$#" -eq 2 ] || fail "usage: $0 <Name.xcarchive> <iOS build directory>"
archive=${1%/}
build=${2%/}
[ -d "$archive/Products/Applications" ] || fail "not an application archive: $archive"
[ -d "$build" ] || fail "no such directory: $build"

apps=("$archive"/Products/Applications/*.app)
[ "${#apps[@]}" -eq 1 ] && [ -d "${apps[0]}" ] \
  || fail "expected exactly one application in $archive/Products/Applications"
app=${apps[0]}
name=$(basename "$app" .app)
exe=$app/$name
[ -f "$exe" ] || fail "no executable at $exe"
dsym=$build/Release-iphoneos/$name.app.dSYM
[ -d "$dsym" ] || fail "no debug symbols at $dsym"

exe_uuid=$(xcrun dwarfdump --uuid "$exe")
dsym_uuid=$(xcrun dwarfdump --uuid "$dsym")
exe_uuid=$(awk '{print $2}' <<<"$exe_uuid")
dsym_uuid=$(awk '{print $2}' <<<"$dsym_uuid")
[ -n "$exe_uuid" ] || fail "the archived executable has no UUID"
[ "$exe_uuid" = "$dsym_uuid" ] \
  || fail "$dsym ($dsym_uuid) does not belong to the archived executable ($exe_uuid): archive again"

# More than the one source file a stripped library build would leave.
units=$(xcrun dwarfdump --debug-info "$dsym")
units=$(grep -c DW_TAG_compile_unit <<<"$units" || true)
[ "$units" -ge 20 ] || fail "$dsym describes only $units source files"

mkdir -p "$archive/dSYMs"
target=$archive/dSYMs/$name.app.dSYM
[ ! -e "$target" ] || fail "$target is already there"
cp -R "$dsym" "$target"
echo "collect_dsym: $name.app.dSYM ($exe_uuid, $units source files) is in $archive/dSYMs"
