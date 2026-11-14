#!/bin/bash
# Builds the App Store package of the iOS application: configure, archive,
# debug symbols, export, checks. Uploads nothing: at the end it prints the
# one command that sends the export to App Store Connect, for the account
# holder to run.
#
# Usage: PINBALL_IOS_TEAM_ID=XXXXXXXXXX tools/ios/archive.sh [options]
#   --source <dir>         the checkout to build (default: the one this
#                          script is in), e.g. a worktree at origin/main
#   --build <dir>          build directory (default: build-ios-release,
#                          relative to the current directory)
#   --build-number <n>     bundle version (default: 1); must be higher than
#                          the last upload's for the same version
#   --allow-dirty          build even if the working tree has changes
#                          (for a trial; the package says which commit it
#                          was built from, and a dirty tree makes that untrue)
#
# The team id is read from the environment and goes to CMake on the command
# line, so it lives in the build directory alone and is never committed.
# Signing is Xcode's automatic signing with -allowProvisioningUpdates: the
# Apple account signed in to Xcode creates or reuses the certificates and
# the App Store provisioning profile. The script reports every provisioning
# profile that appeared while it ran.
#
# Steps:
#   1. configure (cmake/Ios.cmake refuses the placeholder icon and the test
#      exit switch for a build with a team id)
#   2. xcodebuild archive, Release, generic iOS device
#   3. tools/ios/collect_dsym.sh: the .dSYM into the archive
#   4. xcodebuild -exportArchive with packaging/ios/ExportOptions.plist
#      (method app-store-connect, destination export): an .ipa
#   5. tools/ios/check_bundle.sh --release on the application inside the .ipa
#   6. the signature: codesign --verify, an Apple Distribution signature of
#      this team, the entitlements limited to what an App Store build of an
#      app with no capabilities carries, and the embedded profile an App
#      Store profile for this bundle id
#   7. an upload variant of the export options next to the archive, and the
#      command that uses it
#
# macOS with Xcode only. Takes a few minutes (the first configure longer):
# on a shared machine start it detached and read its log.
set -euo pipefail

fail() {
  echo "archive: $*" >&2
  exit 1
}
step() {
  echo
  echo "archive: == $* =="
}

[ "$(uname -s)" = Darwin ] || fail "macOS only"

build=build-ios-release
source_dir=
build_number=1
allow_dirty=no
while [ "$#" -gt 0 ]; do
  case $1 in
    --build) [ "$#" -ge 2 ] || fail "--build needs a directory"; build=$2; shift 2 ;;
    --source) [ "$#" -ge 2 ] || fail "--source needs a directory"; source_dir=$2; shift 2 ;;
    --build-number) [ "$#" -ge 2 ] || fail "--build-number needs a number"; build_number=$2; shift 2 ;;
    --allow-dirty) allow_dirty=yes; shift ;;
    *) fail "unknown argument: $1 (see the usage at the top of $0)" ;;
  esac
done

team=${PINBALL_IOS_TEAM_ID:-}
[ -n "$team" ] || fail "set PINBALL_IOS_TEAM_ID to the Apple developer team id"
[[ $team =~ ^[A-Z0-9]{10}$ ]] || fail "PINBALL_IOS_TEAM_ID must be ten capital letters and digits"
[[ $build_number =~ ^[1-9][0-9]{0,8}$ ]] || fail "--build-number must be a whole number from 1 to 999999999"
case $build in
  ""|/|-*) fail "not a usable build directory: '$build'" ;;
esac

# Xcode, not the command line tools, whatever xcode-select says.
if [ -z "${DEVELOPER_DIR:-}" ]; then
  selected=$(xcode-select -p 2>/dev/null || true)
  if [[ $selected != *.app/Contents/Developer ]] && [ -d /Applications/Xcode.app/Contents/Developer ]; then
    export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
  fi
fi
for tool in cmake xcodebuild codesign plutil security ditto git; do
  command -v "$tool" >/dev/null || fail "not found: $tool"
done
xcode_version=$(xcodebuild -version)
echo "archive: ${xcode_version//$'\n'/, }"

here=$(cd "$(dirname "$0")" && pwd -P)
repo=$(cd "${source_dir:-$here/../..}" && pwd -P) || fail "no such directory: $source_dir"
[ -f "$repo/cmake/Ios.cmake" ] && [ -f "$repo/tools/ios/check_bundle.sh" ] \
  || fail "not a checkout of the game: $repo"
mkdir -p "$build"
build=$(cd "$build" && pwd -P)
cd "$repo"
commit=$(git rev-parse HEAD)
changes=$(git status --porcelain --untracked-files=no)
if [ -n "$changes" ]; then
  if [ "$allow_dirty" = yes ]; then
    echo "archive: WARNING: building commit $commit with local changes:"
    echo "$changes"
  else
    fail "the working tree has changes; commit them, or pass --allow-dirty for a trial:
$changes"
  fi
fi
echo "archive: source $repo at $commit, team $team, build number $build_number"

[ "$build" != "$repo" ] || fail "the build directory must not be the repository itself"
archive=$build/MultiballAcademy.xcarchive
export_dir=$build/export
log_dir=$build/logs
mkdir -p "$log_dir"

# Provisioning profiles before, to report what Xcode creates.
profile_dirs=("$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles"
              "$HOME/Library/MobileDevice/Provisioning Profiles")
list_profiles() {
  local dir
  for dir in "${profile_dirs[@]}"; do
    [ -d "$dir" ] && find "$dir" -maxdepth 1 -type f \( -name '*.mobileprovision' -o -name '*.provisionprofile' \)
  done | LC_ALL=C sort
}
profiles_before=$(list_profiles)

# A provisioning profile, decoded, as XML.
profile_xml() {
  security cms -D -i "$1" 2>/dev/null
}
profile_field() {
  plutil -extract "$2" raw -o - - <<<"$1" 2>/dev/null || true
}

step "1/7 configure"
cmake -S "$repo" -B "$build" -G Xcode -DCMAKE_SYSTEM_NAME=iOS -C "$repo/cmake/IosPreseed.cmake" \
  -DPINBALL_FETCH_DEPS=ON -DPINBALL_WERROR=ON \
  -DPINBALL_IOS_TEAM_ID="$team" -DPINBALL_BUILD_NUMBER="$build_number" \
  -DPINBALL_IOS_TEST_EXIT=OFF > "$log_dir/configure.log" 2>&1 \
  || { tail -n 30 "$log_dir/configure.log" >&2; fail "configure failed (log: $log_dir/configure.log)"; }
product=$build/product.env
[ -f "$product" ] || fail "configure wrote no $product"
product_value() {
  sh "$repo/tools/product_value.sh" "$product" "$1"
}
name=$(product_value PINBALL_APP_NAME)
bundle_id=$(product_value PINBALL_BUNDLE_ID)
version=$(product_value PINBALL_VERSION)
[ "$(product_value PINBALL_BUILD_NUMBER)" = "$build_number" ] || fail "product.env has another build number"
[ "$(product_value PINBALL_IOS_TEST_EXIT)" != ON ] || fail "the build has the test exit switch"
project=$(find "$build" -maxdepth 1 -name '*.xcodeproj' -print -quit)
[ -n "$project" ] || fail "no Xcode project in $build"
echo "archive: $name $version ($build_number), $bundle_id, project $(basename "$project")"

step "2/7 archive"
rm -rf "$archive" "$export_dir"
xcodebuild archive -project "$project" -scheme pinball -configuration Release \
  -destination 'generic/platform=iOS' -archivePath "$archive" -allowProvisioningUpdates \
  > "$log_dir/archive.log" 2>&1 \
  || { grep -E 'error:|warning:|BUILD|ARCHIVE' "$log_dir/archive.log" | tail -n 30 >&2 || true
       fail "archive failed (log: $log_dir/archive.log)"; }
products=$(cd "$archive/Products" && find . -mindepth 2 -maxdepth 2 | LC_ALL=C sort)
[ "$products" = "./Applications/$name.app" ] \
  || fail "the archive must hold the application alone, it holds:
$products"
archive_plist=$archive/Info.plist
[ "$(plutil -extract ApplicationProperties.CFBundleIdentifier raw -o - "$archive_plist")" = "$bundle_id" ] \
  || fail "the archive is not $bundle_id"
echo "archive: $archive"

step "3/7 debug symbols"
bash "$repo/tools/ios/collect_dsym.sh" "$archive" "$build"

step "4/7 export"
xcodebuild -exportArchive -archivePath "$archive" \
  -exportOptionsPlist "$repo/packaging/ios/ExportOptions.plist" \
  -exportPath "$export_dir" -allowProvisioningUpdates > "$log_dir/export.log" 2>&1 \
  || { tail -n 30 "$log_dir/export.log" >&2; fail "export failed (log: $log_dir/export.log)"; }
ipa=$export_dir/$name.ipa
[ -f "$ipa" ] || fail "the export wrote no $ipa"
echo "archive: $ipa ($(stat -f %z "$ipa") bytes)"

step "5/7 check_bundle --release on the exported application"
unpacked=$build/ipa
rm -rf "$unpacked"
mkdir -p "$unpacked"
ditto -x -k "$ipa" "$unpacked"
app=$unpacked/Payload/$name.app
[ -d "$app" ] || fail "no $name.app in the .ipa"
# check_bundle --release wants the symbols beside the bundle.
ditto "$archive/dSYMs/$name.app.dSYM" "$unpacked/Payload/$name.app.dSYM"
bash "$repo/tools/ios/check_bundle.sh" "$app" "$product" "$repo" --release

step "6/7 signature"
codesign --verify --deep --strict --verbose=2 "$app" 2>&1
details=$(codesign -dvv "$app" 2>&1)
authority=$(sed -n 's/^Authority=//p' <<<"$details" | head -n 1)
signed_team=$(sed -n 's/^TeamIdentifier=//p' <<<"$details")
identifier=$(sed -n 's/^Identifier=//p' <<<"$details")
[ "$signed_team" = "$team" ] || fail "signed for team '$signed_team', not $team"
[ "$identifier" = "$bundle_id" ] || fail "signed as '$identifier', not $bundle_id"
[[ $authority == "Apple Distribution: "* || $authority == "iPhone Distribution: "* ]] \
  || fail "not a distribution signature (first authority: '${authority%%:*}')"
echo "archive: signature: distribution certificate of team $signed_team, identifier $identifier"

entitlements=$(codesign -d --entitlements - --xml "$app" 2>/dev/null)
[ -n "$entitlements" ] || fail "codesign printed no entitlements"
keys=$(plutil -convert json -o - - <<<"$entitlements" \
  | python3 -I -c 'import json, sys; print("\n".join(sorted(json.load(sys.stdin))))')
echo "archive: entitlements: ${keys//$'\n'/, }"
# An App Store build of an app with no capabilities: who it is and nothing more.
while IFS= read -r key; do
  case $key in
    application-identifier|com.apple.developer.team-identifier|get-task-allow|beta-reports-active) ;;
    *) fail "unexpected entitlement: $key (the game uses no capability)" ;;
  esac
done <<<"$keys"
[ "$(plutil -extract application-identifier raw -o - - <<<"$entitlements")" = "$team.$bundle_id" ] \
  || fail "application-identifier is not $team.$bundle_id"
task_allow=$(plutil -extract get-task-allow raw -o - - <<<"$entitlements" 2>/dev/null || echo absent)
[ "$task_allow" = false ] || [ "$task_allow" = absent ] \
  || fail "get-task-allow is $task_allow: a debugging build, not one for the App Store"

[ -f "$app/embedded.mobileprovision" ] || fail "no embedded provisioning profile"
embedded=$(profile_xml "$app/embedded.mobileprovision")
[ -n "$embedded" ] || fail "the embedded provisioning profile does not decode"
profile_name=$(profile_field "$embedded" Name)
profile_uuid=$(profile_field "$embedded" UUID)
profile_expiry=$(profile_field "$embedded" ExpirationDate)
profile_app_id=$(profile_field "$embedded" Entitlements.application-identifier)
[ "$profile_app_id" = "$team.$bundle_id" ] || fail "the profile is for '$profile_app_id'"
# An App Store profile lists no devices and does not provision all devices.
if plutil -extract ProvisionedDevices raw -o - - <<<"$embedded" >/dev/null 2>&1; then
  fail "the embedded profile lists devices: it is not an App Store profile"
fi
if [ "$(profile_field "$embedded" ProvisionsAllDevices)" = true ]; then
  fail "the embedded profile is an enterprise profile"
fi
echo "archive: profile: '$profile_name', $profile_uuid, expires $profile_expiry"

profiles_after=$(list_profiles)
created=$(LC_ALL=C comm -13 <(printf '%s\n' "$profiles_before") <(printf '%s\n' "$profiles_after") | sed '/^$/d')
if [ -n "$created" ]; then
  echo "archive: provisioning profiles Xcode added while this ran:"
  while IFS= read -r file; do
    xml=$(profile_xml "$file")
    echo "  $(basename "$file"): '$(profile_field "$xml" Name)', app id $(profile_field "$xml" Entitlements.application-identifier), expires $(profile_field "$xml" ExpirationDate)"
  done <<<"$created"
else
  echo "archive: no provisioning profile was added (Xcode reused existing ones)"
fi

step "7/7 upload command"
upload_options=$build/ExportOptions-upload.plist
cp "$repo/packaging/ios/ExportOptions.plist" "$upload_options"
plutil -replace destination -string upload "$upload_options"
plutil -lint "$upload_options" >/dev/null
[ "$(plutil -extract method raw -o - "$upload_options")" = app-store-connect ] \
  || fail "the export options are not for App Store Connect"

cat <<EOF

archive: done. $name $version ($build_number) from $commit:
  archive  $archive
  package  $ipa
  symbols  $archive/dSYMs/$name.app.dSYM
Nothing has been uploaded. To send this build to App Store Connect, run (one line; it uses the
Apple account signed in to Xcode, the same one that signed it, and no password is stored anywhere):

DEVELOPER_DIR=${DEVELOPER_DIR:-$(xcode-select -p)} xcodebuild -exportArchive -archivePath "$archive" -exportOptionsPlist "$upload_options" -exportPath "$build/upload" -allowProvisioningUpdates

EOF
