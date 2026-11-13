#!/bin/bash
# Runs the test programs of an iOS simulator build on a simulator: the core
# unit tests and the two file handling tests, compiled with the iOS SDK and
# its standard library. CTest cannot do this (it runs programs on the Mac
# itself), so this is what CI calls, and what docs/BUILDING.md names.
#
# If the build was configured with -DPINBALL_IOS_TEST_EXIT=ON it then
# installs the game itself, starts it for 300 frames with SDL's silent audio
# driver, and requires the line the game logs when a timed run ends well
# ("frames=300 steps=N ok"): the assets verified, a window and a renderer
# came up, the simulation ran, and the process ended by itself.
#
# Usage: simulator_tests.sh <build directory> [configuration [device name]]
#   configuration defaults to Release, the device to "iPhone 17".
#
# Picks that device under the newest installed iOS runtime, boots it without
# a window if it is not running, runs the programs in it with
# `xcrun simctl spawn`, and shuts it down again if it was this script that
# booted it. macOS with Xcode only.
set -euo pipefail

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
devices=$(xcrun simctl list devices available -j)
found=$(DEVICE=$device python3 -I -c '
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
' <<<"$devices")
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

here=$(cd "$(dirname "$0")" && pwd)
product_value() {
  sh "$here/../product_value.sh" "$build/product.env" "$1"
}
if [ "$(product_value PINBALL_IOS_TEST_EXIT)" != ON ]; then
  echo "simulator_tests: the game is not started (configure with -DPINBALL_IOS_TEST_EXIT=ON for that)"
  exit 0
fi
name=$(product_value PINBALL_APP_NAME)
identifier=$(product_value PINBALL_BUNDLE_ID)
app=$products/$name.app
[ -d "$app" ] || fail "not built: $app"
# simctl resolves the log's path inside the simulator's own data directory.
data=$HOME/Library/Developer/CoreSimulator/Devices/$udid/data
[ -d "$data" ] || fail "no simulator data directory at $data"
mkdir -p "$data/tmp"
log=$data/tmp/pinball-launch.log
rm -f "$log"
echo "simulator_tests: launching $identifier for 300 frames"
xcrun simctl install "$udid" "$app"
SIMCTL_CHILD_SDL_AUDIO_DRIVER=dummy xcrun simctl launch \
  --stdout=/tmp/pinball-launch.log --stderr=/tmp/pinball-launch.log \
  "$udid" "$identifier" --frames 300
waited=0
until [ -f "$log" ] && grep -q 'frames=[0-9]* steps=[0-9]* ' "$log"; do
  if [ "$waited" -ge 120 ]; then
    [ -f "$log" ] && cat "$log" >&2
    xcrun simctl terminate "$udid" "$identifier" >/dev/null 2>&1 || true
    fail "the game did not finish its timed run within two minutes"
  fi
  sleep 2
  waited=$((waited + 2))
done
result=$(grep 'frames=[0-9]* steps=[0-9]* ' "$log")
echo "$result"
grep -q 'frames=300 steps=[1-9][0-9]* ok' <<<"$result" \
  || fail "the timed run did not end well"
# And the process is gone: nothing in a submitted build may end it, but this
# build was asked to.
sleep 2
running=$(xcrun simctl spawn "$udid" launchctl list)
if grep -q "$identifier" <<<"$running"; then
  xcrun simctl terminate "$udid" "$identifier" >/dev/null 2>&1 || true
  fail "the game logged the end of its run but is still running"
fi
echo "simulator_tests: the game ran 300 frames on $device and ended by itself"
