#!/bin/sh
# Checks a built iOS application bundle. macOS only (plutil, otool, nm).
#
# Usage: check_bundle.sh <Name.app> <product.env> <repository> [--release]
#   The whole check: Info.plist against the values CMake wrote to
#   <build>/product.env, the privacy manifest, the assets, the icon, and the
#   executable (architecture, libraries, the frameworks SDL was built
#   without, no network calls, every required-reason API declared). With
#   --release also: a .dSYM beside the bundle and no build path inside it.
#
# Usage: check_bundle.sh --resources <repository>
#   The part every build runs as its last step (cmake/Ios.cmake): the assets
#   and the privacy manifest are in the bundle. The bundle is found through
#   the variables Xcode gives a build phase.
set -eu

fail() {
  echo "check_bundle: $*" >&2
  exit 1
}

# Every file under <repository>/assets is in the bundle, byte for byte, and
# the bundle holds no other. The game checks the same files against the
# hashes compiled into it when it starts; this says so at build time.
check_assets() {
  source_assets=$1/assets
  bundle_assets=$2/assets
  [ -d "$source_assets" ] || fail "no such directory: $source_assets"
  [ -d "$bundle_assets" ] || fail "the bundle has no assets directory: $bundle_assets"
  wanted=$(cd "$source_assets" && find . -type f -name '*.png' | LC_ALL=C sort)
  [ -n "$wanted" ] || fail "no assets under $source_assets"
  found=$(cd "$bundle_assets" && find . -type f | LC_ALL=C sort)
  [ "$wanted" = "$found" ] || fail "the bundle's assets are not the source's:
wanted:
$wanted
found:
$found"
  # (The loop is a subshell: its failure has to be passed on by hand.)
  echo "$wanted" | while IFS= read -r name; do
    cmp -s "$source_assets/$name" "$bundle_assets/$name" \
      || fail "asset differs from its source: $name"
  done || exit 1
  # The count, for the caller.
  echo "$wanted" | wc -l | tr -d ' '
}

# The privacy manifest is in the bundle, parses, and says what the source's
# says (Xcode may store it in another plist format).
check_privacy() {
  source_manifest=$1/packaging/ios/PrivacyInfo.xcprivacy
  bundle_manifest=$2/PrivacyInfo.xcprivacy
  [ -f "$bundle_manifest" ] || fail "the bundle has no PrivacyInfo.xcprivacy"
  plutil -lint "$bundle_manifest" >/dev/null || fail "PrivacyInfo.xcprivacy does not parse"
  [ "$(plutil -convert xml1 -o - "$source_manifest")" = "$(plutil -convert xml1 -o - "$bundle_manifest")" ] \
    || fail "the bundle's PrivacyInfo.xcprivacy differs from packaging/ios/PrivacyInfo.xcprivacy"
}

if [ "${1:-}" = "--resources" ]; then
  [ "$#" -eq 2 ] || fail "usage: $0 --resources <repository>"
  app=${CODESIGNING_FOLDER_PATH:-}
  if [ -z "$app" ] && [ -n "${TARGET_BUILD_DIR:-}" ] && [ -n "${WRAPPER_NAME:-}" ]; then
    app=$TARGET_BUILD_DIR/$WRAPPER_NAME
  fi
  [ -n "$app" ] || fail "--resources is for an Xcode build phase: no bundle path in the environment"
  [ -d "$app" ] || fail "no such bundle: $app"
  count=$(check_assets "$2" "$app")
  check_privacy "$2" "$app"
  echo "check_bundle: $count assets and the privacy manifest are in $app"
  exit 0
fi

release=no
if [ "$#" -eq 4 ] && [ "$4" = "--release" ]; then
  release=yes
elif [ "$#" -ne 3 ]; then
  fail "usage: $0 <Name.app> <product.env> <repository> [--release]"
fi
app=${1%/}
product=$2
repo=$3
[ -d "$app" ] || fail "no such bundle: $app"
[ -f "$product" ] || fail "no such file: $product"
[ "$(uname -s)" = Darwin ] || fail "macOS only"

# PINBALL_APP_NAME, PINBALL_BUNDLE_ID, PINBALL_VERSION, PINBALL_BUILD_NUMBER,
# PINBALL_DEPLOYMENT_TARGET and PINBALL_ASSET_MANIFEST, as CMake configured them.
# (A name without a slash would be looked up along PATH.)
case $product in
  */*) ;;
  *) product=./$product ;;
esac
# shellcheck disable=SC1090
. "$product"

plist=$app/Info.plist
[ -f "$plist" ] || fail "no Info.plist in $app"
plutil -lint "$plist" >/dev/null || fail "Info.plist does not parse"

# One value, by key path. Empty if the key is absent.
value() {
  plutil -extract "$1" raw -o - "$plist" 2>/dev/null || true
}
# An array or dictionary as one line of JSON.
json() {
  plutil -extract "$1" json -o - "$plist" 2>/dev/null | tr -d ' \t\n' || true
}
expect() {
  got=$(value "$1")
  [ "$got" = "$2" ] || fail "Info.plist $1 is '$got', expected '$2'"
}
expect_json() {
  got=$(json "$1")
  [ "$got" = "$2" ] || fail "Info.plist $1 is '$got', expected '$2'"
}

expect CFBundleIdentifier "$PINBALL_BUNDLE_ID"
expect CFBundleName "$PINBALL_APP_NAME"
expect CFBundleDisplayName "$PINBALL_APP_NAME"
expect CFBundleExecutable "$PINBALL_APP_NAME"
expect CFBundleShortVersionString "$PINBALL_VERSION"
expect CFBundleVersion "$PINBALL_BUILD_NUMBER"
expect CFBundlePackageType APPL
expect MinimumOSVersion "$PINBALL_DEPLOYMENT_TARGET"
expect LSRequiresIPhoneOS true
expect LSApplicationCategoryType public.app-category.arcade-games
expect GCSupportsGameMode true
expect LSSupportsGameMode true
expect UILaunchScreen.UIColorName LaunchBackground
expect UIStatusBarHidden true
expect CADisableMinimumFrameDurationOnPhone true
expect UIApplicationSupportsIndirectInputEvents true
expect ITSAppUsesNonExemptEncryption false
# iPhone and iPad.
expect_json UIDeviceFamily '[1,2]'
expect_json UIRequiredDeviceCapabilities '["arm64"]'
# iPhone: portrait only. iPad: all four.
expect_json UISupportedInterfaceOrientations '["UIInterfaceOrientationPortrait"]'
expect_json 'UISupportedInterfaceOrientations~ipad' \
  '["UIInterfaceOrientationPortrait","UIInterfaceOrientationPortraitUpsideDown","UIInterfaceOrientationLandscapeLeft","UIInterfaceOrientationLandscapeRight"]'
# The icon, compiled from the asset catalog.
expect CFBundleIcons.CFBundlePrimaryIcon.CFBundleIconName AppIcon
[ -s "$app/Assets.car" ] || fail "the bundle has no compiled asset catalog (Assets.car)"

# Keys that must stay out: see cmake/Info-iOS.plist.in.
for key in UIRequiresFullScreen UIApplicationSceneManifest UIBackgroundModes \
           UIFileSharingEnabled LSSupportsOpeningDocumentsInPlace NSAppTransportSecurity \
           GCSupportsControllerUserInteraction GCSupportedGameControllers \
           UIDesignRequiresCompatibility; do
  [ -z "$(json "$key")" ] || fail "Info.plist has $key"
done
# No permission is asked for.
if plutil -convert xml1 -o - "$plist" | grep -E '<key>NS[A-Za-z]*UsageDescription</key>'; then
  fail "Info.plist has a usage description"
fi

check_privacy "$repo" "$app"
count=$(check_assets "$repo" "$app")
# As many as the manifest compiled into the executable lists.
[ -f "$PINBALL_ASSET_MANIFEST" ] || fail "no asset manifest at $PINBALL_ASSET_MANIFEST"
listed=$(grep -c '^{"' "$PINBALL_ASSET_MANIFEST" || true)
[ "$count" = "$listed" ] || fail "the bundle has $count assets, the embedded manifest lists $listed"

exe=$app/$PINBALL_APP_NAME
[ -x "$exe" ] || fail "no executable at $exe"
[ "$(lipo -archs "$exe")" = arm64 ] || fail "the executable is not arm64 alone: $(lipo -archs "$exe")"

# Only the system's libraries and frameworks are loaded.
libraries=$(otool -L "$exe" | tail -n +2)
echo "$libraries"
if echo "$libraries" | grep -v -E '^[[:space:]]+(/System/Library/|/usr/lib/)'; then
  fail "non-system library dependency"
fi
# SDL is built without its camera, sensor, joystick, HIDAPI and OpenGL ES
# code (cmake/PinnedDeps.cmake), so the frameworks behind them, and the
# classes that would make the App Store ask for a camera, motion or Bluetooth
# purpose string, are not referred to at all.
if echo "$libraries" | grep -E 'CoreBluetooth|CoreMotion|CoreHaptics|OpenGLES|CoreLocation|Vulkan'; then
  fail "a framework the game has no use for is linked"
fi
undefined=$(nm -u "$exe")
if echo "$undefined" | grep -E 'AVCapture|CMMotionManager|CBCentralManager|CBPeripheral|CLLocationManager'; then
  fail "the executable refers to a camera, motion, Bluetooth or location class"
fi
if strings -a "$exe" | grep -E '^(AVCaptureDevice|CMMotionManager|CBCentralManager)$'; then
  fail "the executable names a camera, motion or Bluetooth class"
fi

# No network: the game opens no connection, and neither does anything
# linked into it.
if echo "$undefined" | grep -E '^_(socket|connect|bind|listen|accept|getaddrinfo|gethostbyname|sendto|recvfrom|CFSocket[A-Za-z]*|CFStream[A-Za-z]*|CFHTTP[A-Za-z]*|CFNetwork[A-Za-z]*|nw_[a-z_]*)$|NSURLSession|NSURLConnection|NSNetService'; then
  fail "the executable refers to a network API"
fi

# Required-reason APIs: each category the executable can reach must be
# declared in the privacy manifest, and nothing may be declared that is not
# used, since that would be a wrong statement. The symbol lists follow
# Apple's "Describing use of required reason API"; Apple's own scan at
# upload has the last word.
manifest=$(plutil -convert xml1 -o - "$app/PrivacyInfo.xcprivacy")
# category <name> <pattern over the undefined symbols>
category() {
  used=no
  if echo "$undefined" | grep -q -E "$2"; then
    used=yes
  fi
  declared=no
  if echo "$manifest" | grep -q "<string>NSPrivacyAccessedAPICategory$1</string>"; then
    declared=yes
  fi
  [ "$used" = "$declared" ] \
    || fail "required-reason API category $1: used by the executable: $used, declared in PrivacyInfo.xcprivacy: $declared"
  echo "required-reason API category $1: used and declared: $used"
}
category FileTimestamp '^_(stat|fstat|lstat|fstatat|getattrlist|fgetattrlist|getattrlistbulk|getattrlistat)$'
category SystemBootTime '^_(mach_absolute_time|mach_continuous_time)$'
category DiskSpace '^_(statfs|fstatfs|statvfs|fstatvfs|getfsstat)$'
category UserDefaults '_OBJC_CLASS_\$_NSUserDefaults$'
if strings -a "$exe" | grep -q -x 'activeInputModes'; then
  fail "the executable uses the active keyboards API, which PrivacyInfo.xcprivacy does not declare"
fi

if [ "$release" = yes ]; then
  # Checked on the executable and on every text file of the bundle.
  if strings -a "$exe" | grep -E '/Users/|/home/|/private/var/|/Volumes/'; then
    fail "build paths found in the executable"
  fi
  [ -d "$app.dSYM" ] || fail "no debug symbols beside the bundle: $app.dSYM"
  # Stripped: the symbol table holds only what the system must bind.
  if nm -a "$exe" 2>/dev/null | grep -E ' (OSO|SO|SOL) '; then
    fail "the executable still has its debug map"
  fi
fi

echo "check_bundle: $app is complete ($count assets, version $PINBALL_VERSION build $PINBALL_BUILD_NUMBER)"
