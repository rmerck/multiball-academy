#!/bin/bash
# Checks a built iOS application bundle. macOS only (plutil, otool, nm).
#
# Usage: check_bundle.sh <Name.app> <product.env> <repository> [--release]
#   For a simulator or a device build alike. The whole check: Info.plist against the values CMake wrote to
#   <build>/product.env, the privacy manifest, the assets, the icon, and the
#   executable (architecture, libraries, the frameworks SDL was built
#   without, no network calls, every required-reason API declared). With
#   --release also: a .dSYM beside the bundle, no build path inside it, and
#   no launch-argument handling unless the build was configured for tests.
#
# Usage: check_bundle.sh --resources <repository>
#   The part every build runs as its last step (cmake/Ios.cmake): the assets
#   and the privacy manifest are in the bundle. The bundle is found through
#   the variables Xcode gives a build phase.
#
# The output of every tool is taken into a variable before it is searched,
# so a tool that fails stops the check and cannot pass for "nothing found".
set -euo pipefail

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
  wanted=$(cd "$source_assets" && find . -type f -name '*.png')
  wanted=$(LC_ALL=C sort <<<"$wanted")
  [ -n "$wanted" ] || fail "no assets under $source_assets"
  found=$(cd "$bundle_assets" && find . -type f)
  found=$(LC_ALL=C sort <<<"$found")
  [ "$wanted" = "$found" ] || fail "the bundle's assets are not the source's:
wanted:
$wanted
found:
$found"
  count=0
  while IFS= read -r name; do
    cmp -s "$source_assets/$name" "$bundle_assets/$name" \
      || fail "asset differs from its source: $name"
    count=$((count + 1))
  done <<<"$wanted"
  # The count, for the caller.
  echo "$count"
}

# The privacy manifest is in the bundle, parses, and says what the source's
# says (Xcode may store it in another plist format).
check_privacy() {
  source_manifest=$1/packaging/ios/PrivacyInfo.xcprivacy
  bundle_manifest=$2/PrivacyInfo.xcprivacy
  [ -f "$bundle_manifest" ] || fail "the bundle has no PrivacyInfo.xcprivacy"
  plutil -lint "$bundle_manifest" >/dev/null || fail "PrivacyInfo.xcprivacy does not parse"
  source_text=$(plutil -convert xml1 -o - "$source_manifest")
  bundle_text=$(plutil -convert xml1 -o - "$bundle_manifest")
  [ -n "$source_text" ] && [ "$source_text" = "$bundle_text" ] \
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

# The values CMake configured, read as text (tools/product_value.sh).
here=$(cd "$(dirname "$0")" && pwd)
product_value() {
  sh "$here/../product_value.sh" "$product" "$1"
}
PINBALL_APP_NAME=$(product_value PINBALL_APP_NAME)
PINBALL_HOME_NAME=$(product_value PINBALL_HOME_NAME)
PINBALL_BUNDLE_ID=$(product_value PINBALL_BUNDLE_ID)
PINBALL_VERSION=$(product_value PINBALL_VERSION)
PINBALL_BUILD_NUMBER=$(product_value PINBALL_BUILD_NUMBER)
PINBALL_DEPLOYMENT_TARGET=$(product_value PINBALL_DEPLOYMENT_TARGET)
PINBALL_ASSET_MANIFEST=$(product_value PINBALL_ASSET_MANIFEST)
PINBALL_IOS_TEST_EXIT=$(product_value PINBALL_IOS_TEST_EXIT)
case $PINBALL_IOS_TEST_EXIT in
  ON | OFF) ;;
  *) fail "PINBALL_IOS_TEST_EXIT in $product is '$PINBALL_IOS_TEST_EXIT', not ON or OFF: reconfigure" ;;
esac
for required in "$PINBALL_APP_NAME" "$PINBALL_HOME_NAME" "$PINBALL_BUNDLE_ID" "$PINBALL_VERSION" \
                "$PINBALL_BUILD_NUMBER" "$PINBALL_DEPLOYMENT_TARGET" "$PINBALL_ASSET_MANIFEST"; do
  [ -n "$required" ] || fail "a value is empty in $product"
done

plist=$app/Info.plist
[ -f "$plist" ] || fail "no Info.plist in $app"
plutil -lint "$plist" >/dev/null || fail "Info.plist does not parse"
plist_xml=$(plutil -convert xml1 -o - "$plist")
[ -n "$plist_xml" ] || fail "Info.plist converts to nothing"

# One value, by key path. Empty if the key is absent (plutil then fails,
# which is the one failure that is expected here).
value() {
  plutil -extract "$1" raw -o - "$plist" 2>/dev/null || true
}
# An array or dictionary as one line of JSON. Empty if the key is absent.
json() {
  local text
  text=$(plutil -extract "$1" json -o - "$plist" 2>/dev/null) || text=
  tr -d ' \t\n' <<<"$text"
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
# The short name under the icon on the home screen.
expect CFBundleDisplayName "$PINBALL_HOME_NAME"
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
# (Top-level keys, as they appear in the XML form.)
# The URL, document and activity keys would each open a way into the game
# from outside: SDL's application delegate turns any URL or document the
# system hands it into a drop event, which the game does not expect.
for key in UIRequiresFullScreen UIApplicationSceneManifest UIBackgroundModes \
           UIFileSharingEnabled LSSupportsOpeningDocumentsInPlace NSAppTransportSecurity \
           GCSupportsControllerUserInteraction GCSupportedGameControllers \
           UIDesignRequiresCompatibility CFBundleURLTypes CFBundleDocumentTypes \
           UTExportedTypeDeclarations UTImportedTypeDeclarations NSUserActivityTypes \
           UISupportsDocumentBrowser LSApplicationQueriesSchemes NSBonjourServices; do
  if grep -q "<key>$key</key>" <<<"$plist_xml"; then
    fail "Info.plist has $key"
  fi
done
# No permission is asked for.
if grep -E '<key>NS[A-Za-z]*UsageDescription</key>' <<<"$plist_xml"; then
  fail "Info.plist has a usage description"
fi

check_privacy "$repo" "$app"
count=$(check_assets "$repo" "$app")
# As many as the manifest compiled into the executable lists.
[ -f "$PINBALL_ASSET_MANIFEST" ] || fail "no asset manifest at $PINBALL_ASSET_MANIFEST"
manifest_text=$(cat "$PINBALL_ASSET_MANIFEST")
listed=$(grep -c '^{"' <<<"$manifest_text" || true)
[ "$count" = "$listed" ] || fail "the bundle has $count assets, the embedded manifest lists $listed"

exe=$app/$PINBALL_APP_NAME
[ -x "$exe" ] || fail "no executable at $exe"
archs=$(lipo -archs "$exe")
[ "$archs" = arm64 ] || fail "the executable is not arm64 alone: $archs"

# Only the system's libraries and frameworks are loaded.
libraries=$(otool -L "$exe")
libraries=$(tail -n +2 <<<"$libraries")
echo "$libraries"
[ -n "$libraries" ] || fail "otool lists no library for the executable"
if grep -v -E '^[[:space:]]+(/System/Library/|/usr/lib/)' <<<"$libraries"; then
  fail "non-system library dependency"
fi
# SDL is built without its camera, sensor, joystick, HIDAPI and OpenGL ES
# code (cmake/PinnedDeps.cmake), so the frameworks behind them, and the
# classes that would make the App Store ask for a camera, motion or Bluetooth
# purpose string, are not referred to at all.
# The Swift runtime's overlays (/usr/lib/swift/libswift*.dylib) are not
# frameworks: the review request's Swift (src/platform/ios/Review.swift)
# auto-links, weakly, the overlay of every module UIKit and StoreKit bring
# in, CoreLocation's among them. They hold no use of the framework; the
# class checks below look at what the executable itself refers to.
frameworks=$(grep -v -E '^[[:space:]]+/usr/lib/swift/' <<<"$libraries" || true)
if grep -E 'CoreBluetooth|CoreMotion|CoreHaptics|OpenGLES|CoreLocation|Vulkan' <<<"$frameworks"; then
  fail "a framework the game has no use for is linked"
fi
undefined=$(nm -u "$exe")
[ -n "$undefined" ] || fail "nm lists no undefined symbol for the executable"
all_strings=$(strings -a "$exe")
[ -n "$all_strings" ] || fail "strings finds nothing in the executable"
if grep -E 'AVCapture|CMMotionManager|CBCentralManager|CBPeripheral|CLLocationManager' <<<"$undefined"; then
  fail "the executable refers to a camera, motion, Bluetooth or location class"
fi
if grep -E '^(AVCaptureDevice|CMMotionManager|CBCentralManager)$' <<<"$all_strings"; then
  fail "the executable names a camera, motion or Bluetooth class"
fi

# No network: the game opens no connection, and neither does anything
# linked into it.
if grep -E '^_(socket|connect|bind|listen|accept|getaddrinfo|gethostbyname|sendto|recvfrom|CFSocket[A-Za-z]*|CFStream[A-Za-z]*|CFHTTP[A-Za-z]*|CFNetwork[A-Za-z]*|nw_[a-z_]*)$|NSURLSession|NSURLConnection|NSNetService' <<<"$undefined"; then
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
  if grep -q -E "$2" <<<"$undefined"; then
    used=yes
  fi
  declared=no
  if grep -q "<string>NSPrivacyAccessedAPICategory$1</string>" <<<"$manifest"; then
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
if grep -q -x 'activeInputModes' <<<"$all_strings"; then
  fail "the executable uses the active keyboards API, which PrivacyInfo.xcprivacy does not declare"
fi

if [ "$release" = yes ]; then
  if grep -E '/Users/|/home/|/private/var/|/Volumes/' <<<"$all_strings"; then
    fail "build paths found in the executable"
  fi
  # A Release application ignores its launch arguments (src/main_ios.cpp):
  # the code that reads them is compiled out, and with it the options'
  # names (--frames, --scene). Only a build configured for tests
  # (PINBALL_IOS_TEST_EXIT) keeps them.
  if [ "$PINBALL_IOS_TEST_EXIT" = OFF ] && grep -q -e '--frames' -e '--scene' <<<"$all_strings"; then
    fail "the executable reads launch arguments, but the build was not configured for tests"
  fi
  # The debug symbols are beside the bundle and are this executable's.
  [ -d "$app.dSYM" ] || fail "no debug symbols beside the bundle: $app.dSYM"
  exe_uuid=$(xcrun dwarfdump --uuid "$exe")
  dsym_uuid=$(xcrun dwarfdump --uuid "$app.dSYM")
  exe_uuid=$(awk '{print $2}' <<<"$exe_uuid")
  dsym_uuid=$(awk '{print $2}' <<<"$dsym_uuid")
  [ -n "$exe_uuid" ] && [ "$exe_uuid" = "$dsym_uuid" ] \
    || fail "the .dSYM ($dsym_uuid) is not the executable's ($exe_uuid)"
  # Stripped: no debug map is left in the symbol table.
  symbols=$(nm -a "$exe")
  if grep -E ' (OSO|SO|SOL) ' <<<"$symbols"; then
    fail "the executable still has its debug map"
  fi
fi

echo "check_bundle: $app is complete ($count assets, version $PINBALL_VERSION build $PINBALL_BUILD_NUMBER)"
