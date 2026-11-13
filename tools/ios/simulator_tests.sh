#!/bin/sh
# Runs the test programs of an iOS simulator build on a simulator: the core
# unit tests and the two file handling tests, compiled with the iOS SDK and
# its standard library. CTest cannot do this (it runs programs on the Mac
# itself), so this is what CI calls, and what docs/BUILDING.md names.
#
# Usage: simulator_tests.sh <build directory> [configuration [device name]]
#   configuration defaults to Release, the device to "iPhone 17".
#
# Picks that device under the newest installed iOS runtime, boots it without
# a window if it is not running, runs the programs in it with
# `xcrun simctl spawn`, and shuts it down again if it was this script that
# booted it. macOS with Xcode only.
set -eu

fail() {
  echo "simulator_tests: $*" >&2
  exit 1
}

[ "$#" -ge 1 ] && [ "$#" -le 3 ] || fail "usage: $0 <build directory> [configuration [device name]]"
[ "$(uname -s)" = Darwin ] || fail "macOS only"
build=$(cd "$1" && pwd -P) || fail "no such directory: $1"
configuration=${2:-Release}
device=${3:-iPhone 17}
products=$build/$configuration-iphonesimulator
[ -d "$products" ] || fail "no simulator build in $products"
for test in core_tests safefile_tests safefile_fallback_tests; do
  [ -x "$products/$test.app/$test" ] || fail "not built: $products/$test.app/$test"
done

# "<udid> <state>" of the named device under the newest iOS runtime that has it.
found=$(xcrun simctl list devices available -j | DEVICE=$device python3 -I -c '
import json, os, re, sys
best = None
for runtime, devices in json.load(sys.stdin)["devices"].items():
    version = re.search(r"SimRuntime\.iOS-(\d+)-(\d+)", runtime)
    if not version:
        continue
    for entry in devices:
        if entry.get("isAvailable") and entry.get("name") == os.environ["DEVICE"]:
            key = (int(version[1]), int(version[2]))
            if best is None or key > best[0]:
                best = (key, entry["udid"], entry.get("state", ""))
if best:
    print(best[1], best[2])
')
[ -n "$found" ] || fail "no available simulator named '$device'"
udid=${found%% *}
state=${found#* }
echo "simulator_tests: $device $udid ($state)"

booted_here=no
scratch=$(mktemp -d "${TMPDIR:-/tmp}/simulator_tests.XXXXXX")
finish() {
  rm -rf "$scratch"
  if [ "$booted_here" = yes ]; then
    xcrun simctl shutdown "$udid" || true
  fi
}
trap finish EXIT
trap 'exit 1' HUP INT TERM

if [ "$state" != Booted ]; then
  xcrun simctl boot "$udid"
  booted_here=yes
fi
# Returns once the system inside has finished starting.
xcrun simctl bootstatus "$udid" >/dev/null

echo "simulator_tests: core_tests"
xcrun simctl spawn "$udid" "$products/core_tests.app/core_tests"
# The file handling tests want a directory that does not exist yet.
echo "simulator_tests: safefile_tests"
xcrun simctl spawn "$udid" "$products/safefile_tests.app/safefile_tests" "$scratch/safefile_scratch"
echo "simulator_tests: safefile_fallback_tests"
xcrun simctl spawn "$udid" "$products/safefile_fallback_tests.app/safefile_fallback_tests" \
  "$scratch/safefile_fallback_scratch"
echo "simulator_tests: all three passed on $device"
