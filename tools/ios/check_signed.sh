#!/bin/bash
# Checks a signed iOS application, or the .ipa exported from an archive,
# before it is uploaded. macOS only (codesign, plutil, ditto).
#
# Usage: check_signed.sh <Name.app | Name.ipa> <product.env> [--distribution]
#
# The game asks for no capability, so its signature may carry only what
# signing adds by itself: the application identifier, the team, the keychain
# group of that identifier, get-task-allow, and for a build for TestFlight or
# the App Store beta-reports-active. Anything else (an app group, iCloud,
# push, a data protection class, a network extension) is a capability that
# was switched on somewhere and must be explained before it ships. Also: the
# signature verifies over the whole bundle, it is for the bundle id CMake
# configured, and the bundle holds no framework, plug-in or extension.
#
# With --distribution (an .ipa exported for App Store Connect): signed by an
# Apple distribution certificate, get-task-allow false, and a provisioning
# profile inside.
set -euo pipefail

fail() {
  echo "check_signed: $*" >&2
  exit 1
}

distribution=no
if [ "$#" -eq 3 ] && [ "$3" = "--distribution" ]; then
  distribution=yes
elif [ "$#" -ne 2 ]; then
  fail "usage: $0 <Name.app | Name.ipa> <product.env> [--distribution]"
fi
[ "$(uname -s)" = Darwin ] || fail "macOS only"
input=${1%/}
product=$2
[ -f "$product" ] || fail "no such file: $product"
here=$(cd "$(dirname "$0")" && pwd)
bundle_id=$(sh "$here/../product_value.sh" "$product" PINBALL_BUNDLE_ID)
[ -n "$bundle_id" ] || fail "no bundle id in $product"

scratch=$(mktemp -d "${TMPDIR:-/tmp}/check_signed.XXXXXX")
trap 'rm -rf "$scratch"' EXIT
case $input in
  *.ipa)
    [ -f "$input" ] || fail "no such file: $input"
    ditto -x -k "$input" "$scratch/ipa"
    apps=$(find "$scratch/ipa/Payload" -maxdepth 1 -name '*.app' -type d)
    [ "$(grep -c . <<<"$apps")" = 1 ] || fail "the .ipa does not hold exactly one application"
    app=$apps
    ;;
  *.app)
    [ -d "$input" ] || fail "no such bundle: $input"
    app=$input
    ;;
  *) fail "not an .app or an .ipa: $input" ;;
esac

codesign --verify --deep --strict "$app" 2>"$scratch/verify" \
  || fail "the signature does not verify: $(cat "$scratch/verify")"
for part in Frameworks PlugIns Extensions Watch AppClips SystemExtensions; do
  [ ! -e "$app/$part" ] || fail "the bundle holds $part/, which the game has none of"
done

codesign -d --entitlements - --xml "$app" >"$scratch/entitlements.plist" 2>/dev/null || true
[ -s "$scratch/entitlements.plist" ] || fail "no entitlements in the signature: is it signed for a team?"
plutil -convert json -o "$scratch/entitlements.json" "$scratch/entitlements.plist" \
  || fail "the entitlements do not parse"
# One line per entitlement: key, a tab, its value as JSON.
listing=$(python3 -I -c '
import json, sys
for key, value in sorted(json.load(open(sys.argv[1])).items()):
    print(key + "\t" + json.dumps(value, separators=(",", ":")))
' "$scratch/entitlements.json")
echo "$listing"
value_of() {
  awk -F '\t' -v key="$1" '$1 == key { print $2 }' <<<"$listing"
}

app_id=$(value_of application-identifier)
app_id=${app_id//\"/}
[ -n "$app_id" ] || fail "no application-identifier"
team=${app_id%%.*}
[[ $team =~ ^[A-Z0-9]{10}$ ]] || fail "application-identifier does not start with a team id: $app_id"
[ "$app_id" = "$team.$bundle_id" ] || fail "application-identifier is $app_id, expected $team.$bundle_id"

while IFS=$'\t' read -r key value; do
  case $key in
    application-identifier) ;;
    com.apple.developer.team-identifier)
      [ "$value" = "\"$team\"" ] || fail "team-identifier $value is not $team" ;;
    keychain-access-groups)
      [ "$value" = "[\"$team.$bundle_id\"]" ] || fail "keychain-access-groups is $value" ;;
    get-task-allow)
      [ "$value" = true ] || [ "$value" = false ] || fail "get-task-allow is $value" ;;
    beta-reports-active)
      [ "$value" = true ] || fail "beta-reports-active is $value" ;;
    *) fail "entitlement $key is not one signing adds by itself: the game asks for no capability" ;;
  esac
done <<<"$listing"

if [ "$distribution" = yes ]; then
  [ "$(value_of get-task-allow)" != true ] || fail "get-task-allow is true: a debugger could attach"
  codesign -dvv "$app" >"$scratch/signature" 2>&1 || true
  grep -q -E '^Authority=(Apple|iPhone) Distribution' "$scratch/signature" \
    || fail "not signed with a distribution certificate: $(grep '^Authority=' "$scratch/signature" | head -1)"
  [ -f "$app/embedded.mobileprovision" ] || fail "no provisioning profile in the bundle"
fi

echo "check_signed: $app is signed for $team.$bundle_id with no capability beyond signing's own"
