#!/bin/bash
# Captures raw App Store screenshots of the real application in the iOS
# simulators, for tools/store/compose.py. macOS with Xcode only.
#
#   capture.sh sets
#       the screenshot sets, the simulator each one is taken on, and its size
#   capture.sh start <set> <Name.app>
#       boots that simulator without a window (one at a time), installs the
#       application, starts it with SDL's silent audio driver, and prints the
#       touch plan for the set
#   capture.sh shot <set> <name> [raw directory]
#       one screenshot to <raw>/<set>/<name>.png (default raw: store-raw)
#   capture.sh burst <set> <name> <seconds> [interval] [raw directory]
#       a screenshot every interval seconds (default 1) for that long, to
#       <raw>/<set>/<name>-NNN.png; run it detached while the touches are
#       played, then keep the best frame as <name>.png
#   capture.sh plan <set>
#       the touch plan again
#   capture.sh stop <set>
#       ends the application and shuts the simulator down if this script
#       booted it
#
# Touches are not sent by this script: simctl has no touch input. They are
# played with Xcode's device interaction tool (DeviceInteractionSynthesize in
# the Xcode MCP tools), whose commands are, in points:
#   t X Y [seconds]                 tap, or hold that long
#   mt [x y, x y] seconds           several fingers at once, held
#   w seconds                       wait
# chained in one string. `plan` prints them for the set, from the control
# positions measured on an iPhone 18 Pro (INTEGRATION.md) scaled to the
# device: check each against a first screenshot before relying on it.
#
# The Release application ignores launch arguments and has no demo state,
# so every screen is reached by playing. Use the build that is to be
# submitted (configured without PINBALL_IOS_TEST_EXIT) for simulator: the
# same code, art and rules.
#
# On the shared Mac take the lock first (mkdir /tmp/ios-mac.lock) and boot
# one simulator at a time.
set -euo pipefail

fail() {
  echo "capture: $*" >&2
  exit 1
}

[ "$(uname -s)" = Darwin ] || fail "macOS only"
if [ -z "${DEVELOPER_DIR:-}" ] && [ -d /Applications/Xcode.app/Contents/Developer ]; then
  export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi

identifier=io.github.rmerck.multiballacademy
state_dir=${TMPDIR:-/tmp}/multiball-capture
mkdir -p "$state_dir"

# set -> simulator name, native size (portrait), points wide
device_of() {
  case $1 in
    iphone-6.3) echo "iPhone 18 Pro|1206x2622|402" ;;
    iphone-6.9) echo "iPhone 18 Pro Max|1320x2868|440" ;;
    ipad-13) echo "iPad Pro 13-inch (M5)|2064x2752|1032" ;;
    duo-outer) echo "iPhone Duo|1398x2034|0" ;;
    duo-inner) echo "iPhone Duo|2007x2853|0" ;;
    *) fail "unknown set: $1 (iphone-6.3, iphone-6.9, ipad-13, duo-outer, duo-inner)" ;;
  esac
}

# The udid of a named device under the newest iOS runtime.
udid_of() {
  local devices
  devices=$(xcrun simctl list devices available -j)
  DEVICE=$1 python3 -I -c '
import json, os, re, sys
best = None
for runtime, devices in json.load(sys.stdin)["devices"].items():
    v = re.search(r"SimRuntime\.iOS-(\d+)-(\d+)", runtime)
    if not v:
        continue
    for d in devices:
        if d.get("isAvailable") and d.get("name") == os.environ["DEVICE"]:
            key = (int(v[1]), int(v[2]), d["udid"])
            if best is None or key > best:
                best = key
print(best[2] if best else "")
' <<<"$devices"
}

state_of() {
  xcrun simctl list devices -j | UDID=$1 python3 -I -c '
import json, os, sys
for ds in json.load(sys.stdin)["devices"].values():
    for d in ds:
        if d["udid"] == os.environ["UDID"]:
            print(d["state"])
'
}

plan() {
  local set=$1 info points
  info=$(device_of "$set")
  points=${info##*|}
  python3 -I - "$set" "$points" <<'PLAN'
import sys
set_name, points = sys.argv[1], float(sys.argv[2])
# Measured on iPhone 18 Pro, 402 x 874 points (INTEGRATION.md): the centre
# button at (201, 717), the flipper pads at y 792. As fractions of the screen.
W0, H0 = 402.0, 874.0
heights = {"iphone-6.3": 874, "iphone-6.9": 956, "ipad-13": 1376}
if points <= 0 or set_name not in heights:
    print("No measured positions for %s: take a screenshot after start and read the" % set_name)
    print("control positions from it (points = pixels / scale).")
    sys.exit(0)
w, h = points, heights[set_name]
def at(fx, fy):
    return "%d %d" % (round(fx * w), round(fy * h))
launch = at(201 / W0, 717 / H0)
left = at(102 / W0, 792 / H0)
right = at(300 / W0, 792 / H0)
print("Touch plan for %s (%d x %d points). Check every position on a first screenshot." % (set_name, w, h))
print("  launch (hold, then let go):   t %s 1.0" % launch)
print("  left flipper tap:             t %s 0.15" % left)
print("  right flipper tap:            t %s 0.15" % right)
print("  both flippers held 2 s:       mt [%s, %s] 2" % (left, right))
rally = " ".join("t %s 0.12 w 0.35 t %s 0.12 w 0.35" % (left, right) for _ in range(8))
print("  a rally (repeat while a burst runs):")
print("    " + rally)
print()
print("Shots (tools/store/shots.txt):")
print("  table      : right after start: 'w 2', then shot. Ball on the plunger, LAUNCH THE BALL.")
print("  about      : tap the pause button (top left of the score strip), SCORES & ABOUT, the ABOUT tab; shot.")
print("  course     : launch, then rallies with a burst running; keep a frame with a course name and")
print("               goal line in the course window (the centre shot starts courses).")
print("  hangar     : as course; keep a frame with a ball parked in a Hangar bay (BALL 1 LOCKED).")
print("  multiball  : as course; keep a frame with three balls and ACADEMY MULTIBALL.")
print("  graduation : needs all six courses in one game; if no frame shows it, use the fallback in shots.txt.")
PLAN
}

cmd=${1:-}
case $cmd in
  sets)
    for s in iphone-6.3 iphone-6.9 ipad-13 duo-outer duo-inner; do
      info=$(device_of "$s")
      name=${info%%|*}
      rest=${info#*|}
      udid=$(udid_of "$name")
      printf '%-11s %-24s %-10s %s\n' "$s" "$name" "${rest%%|*}" "${udid:-not installed}"
    done
    ;;
  start)
    [ "$#" -eq 3 ] || fail "usage: $0 start <set> <Name.app>"
    set=$2
    app=${3%/}
    [ -d "$app" ] || fail "no such application: $app"
    info=$(device_of "$set")
    name=${info%%|*}
    udid=$(udid_of "$name")
    [ -n "$udid" ] || fail "no simulator named '$name'"
    booted=$(xcrun simctl list devices booted -j | python3 -I -c '
import json, sys
print(sum(1 for ds in json.load(sys.stdin)["devices"].values() for d in ds if d.get("state") == "Booted"))')
    state=$(state_of "$udid")
    if [ "$state" != Booted ]; then
      [ "$booted" = 0 ] || fail "another simulator is booted: one at a time (xcrun simctl list devices booted)"
      xcrun simctl boot "$udid"
      echo yes > "$state_dir/$udid.booted-here"
    fi
    xcrun simctl bootstatus "$udid" >/dev/null
    xcrun simctl install "$udid" "$app"
    xcrun simctl terminate "$udid" "$identifier" >/dev/null 2>&1 || true
    # The first launch after a boot sometimes returns no process: retry once.
    SIMCTL_CHILD_SDL_AUDIO_DRIVER=dummy xcrun simctl launch "$udid" "$identifier" >/dev/null \
      || { sleep 8; SIMCTL_CHILD_SDL_AUDIO_DRIVER=dummy xcrun simctl launch "$udid" "$identifier" >/dev/null; }
    echo "capture: $name $udid: $identifier running"
    plan "$set"
    ;;
  shot|burst)
    set=${2:-}
    shot=${3:-}
    [ -n "$set" ] && [ -n "$shot" ] || fail "usage: $0 $cmd <set> <name> ..."
    [[ $shot =~ ^[a-z0-9-]+$ ]] || fail "a shot name is lower-case letters, digits and dashes"
    info=$(device_of "$set")
    name=${info%%|*}
    rest=${info#*|}
    size=${rest%%|*}
    udid=$(udid_of "$name")
    [ "$(state_of "$udid")" = Booted ] || fail "$name is not booted: capture.sh start first"
    if [ "$cmd" = shot ]; then
      raw=${4:-store-raw}
      seconds=0
      interval=1
    else
      seconds=${4:-}
      interval=${5:-1}
      raw=${6:-store-raw}
      [[ $seconds =~ ^[0-9]+$ ]] && [ "$seconds" -gt 0 ] || fail "burst needs a number of seconds"
      [[ $interval =~ ^[0-9]+(\.[0-9]+)?$ ]] || fail "the interval is a number of seconds"
    fi
    mkdir -p "$raw/$set"
    take() {
      local out=$1 got
      # --mask=ignored: the whole rectangle, no rounded-corner alpha.
      xcrun simctl io "$udid" screenshot --type=png --mask=ignored "$out" >/dev/null 2>&1
      got=$(sips -g pixelWidth -g pixelHeight "$out" | awk '/pixelWidth/{w=$2} /pixelHeight/{h=$2} END{print w "x" h}')
      if [ "$got" != "$size" ] && [ "$got" != "${size#*x}x${size%x*}" ]; then
        echo "capture: WARNING: $out is $got, the set wants $size (portrait) or its landscape" >&2
      fi
      echo "$out $got"
    }
    if [ "$cmd" = shot ]; then
      take "$raw/$set/$shot.png"
    else
      end=$((SECONDS + seconds))
      n=0
      while [ "$SECONDS" -lt "$end" ]; do
        n=$((n + 1))
        take "$(printf '%s/%s/%s-%03d.png' "$raw" "$set" "$shot" "$n")"
        sleep "$interval"
      done
    fi
    ;;
  plan)
    [ "$#" -eq 2 ] || fail "usage: $0 plan <set>"
    plan "$2"
    ;;
  stop)
    [ "$#" -eq 2 ] || fail "usage: $0 stop <set>"
    info=$(device_of "$2")
    udid=$(udid_of "${info%%|*}")
    [ -n "$udid" ] || fail "no such simulator"
    xcrun simctl terminate "$udid" "$identifier" >/dev/null 2>&1 || true
    if [ -f "$state_dir/$udid.booted-here" ]; then
      xcrun simctl shutdown "$udid" || true
      rm -f "$state_dir/$udid.booted-here"
      echo "capture: shut down"
    else
      echo "capture: left running (it was booted before start)"
    fi
    ;;
  *)
    fail "usage: $0 sets | start <set> <Name.app> | shot <set> <name> [raw] | burst <set> <name> <seconds> [interval] [raw] | plan <set> | stop <set>"
    ;;
esac
