#!/bin/bash
# Checks a signed iOS application, or the .ipa exported from an archive,
# before it is uploaded. macOS (codesign, security, ditto) and Python 3.
#
# Usage: check_signed.sh <Name.app | Name.ipa> <product.env> [--distribution] [--team TEAMID]
#   The team may also come from the environment, PINBALL_IOS_TEAM_ID. It is
#   required with --distribution.
#
# The game asks for no capability, so its signature may carry only what
# signing adds by itself: the application identifier, the team, the keychain
# group of that identifier, get-task-allow, and for a build for TestFlight or
# the App Store beta-reports-active. Anything else (an app group, iCloud,
# push, a data protection class, a network extension) is a capability that
# was switched on somewhere and must be explained before it ships. Also: the
# signature verifies over the whole bundle, it is for the bundle id CMake
# configured (and the team, when given), and the bundle holds no framework,
# plug-in or extension.
#
# With --distribution (an .ipa exported for App Store Connect): signed by an
# Apple Distribution certificate of the team, get-task-allow false,
# beta-reports-active true, and a provisioning profile inside that is the
# team's, for this identifier, for no particular devices and not expired.
set -euo pipefail

fail() {
  echo "check_signed: $*" >&2
  exit 1
}

usage="usage: $0 <Name.app | Name.ipa> <product.env> [--distribution] [--team TEAMID]"
[ "$#" -ge 2 ] || fail "$usage"
input=${1%/}
product=$2
shift 2
distribution=no
team_given=${PINBALL_IOS_TEAM_ID:-}
while [ "$#" -gt 0 ]; do
  case $1 in
    --distribution) distribution=yes ;;
    --team)
      [ "$#" -ge 2 ] || fail "$usage"
      team_given=$2
      shift
      ;;
    *) fail "$usage" ;;
  esac
  shift
done
if [ -n "$team_given" ] && ! [[ $team_given =~ ^[A-Z0-9]{10}$ ]]; then
  fail "the team id must be ten capital letters and digits: '$team_given'"
fi
if [ "$distribution" = yes ] && [ -z "$team_given" ]; then
  fail "--distribution needs the team: --team TEAMID or PINBALL_IOS_TEAM_ID"
fi
[ "$(uname -s)" = Darwin ] || fail "macOS only"
[ -f "$product" ] || fail "no such file: $product"
here=$(cd "$(dirname "$0")" && pwd)
bundle_id=$(sh "$here/../product_value.sh" "$product" PINBALL_BUNDLE_ID)
[ -n "$bundle_id" ] || fail "no bundle id in $product"

scratch=$(mktemp -d "${TMPDIR:-/tmp}/check_signed.XXXXXX")
trap 'rm -rf "$scratch"' EXIT
case $input in
  *.ipa)
    [ -f "$input" ] || fail "no such file: $input"
    ditto -x -k "$input" "$scratch/ipa" || fail "the .ipa does not unpack"
    apps=$(find "$scratch/ipa/Payload" -maxdepth 1 -name '*.app' -type d)
    [ -n "$apps" ] && [ "$(grep -c . <<<"$apps")" = 1 ] \
      || fail "the .ipa does not hold exactly one application"
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
# One line per entitlement: key, a tab, its value as JSON. Read as data,
# never run.
# Keys are held to plain names here, before they are printed, so that a key
# holding a tab or a line break cannot pass for another key and value.
listing=$(python3 -I -c '
import json, plistlib, re, sys
with open(sys.argv[1], "rb") as f:
    data = plistlib.load(f)
if not isinstance(data, dict):
    sys.exit("check_signed: the entitlements are not a dictionary")
for key in data:
    if not re.fullmatch(r"[A-Za-z0-9.-]+", key):
        sys.exit("check_signed: an entitlement key has characters an entitlement never has: %r" % key)
for key, value in sorted(data.items()):
    print(key + "\t" + json.dumps(value, separators=(",", ":"), default=str))
' "$scratch/entitlements.plist") || fail "the entitlements are not acceptable (see above)"
[ -n "$listing" ] || fail "the entitlements are empty"
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
if [ -n "$team_given" ] && [ "$team" != "$team_given" ]; then
  fail "signed for team $team, expected $team_given"
fi

while IFS=$'\t' read -r key value; do
  # Plain names only, so a key cannot pass for another in the checks below.
  [[ $key =~ ^[A-Za-z0-9.-]+$ ]] || fail "an entitlement key has characters an entitlement never has"
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
  [ "$(value_of get-task-allow)" = false ] || fail "get-task-allow is not false: a debugger could attach"
  [ "$(value_of beta-reports-active)" = true ] \
    || fail "beta-reports-active is not true: not signed for App Store Connect"
  codesign -dvv "$app" >"$scratch/signature" 2>&1 || fail "codesign cannot describe the signature"
  authority=$(grep -m1 '^Authority=' "$scratch/signature" || true)
  [[ $authority =~ ^Authority=Apple\ Distribution:\ .+\ \(([A-Z0-9]{10})\)$ ]] \
    || fail "not signed with an Apple Distribution certificate: '$authority'"
  [ "${BASH_REMATCH[1]}" = "$team_given" ] \
    || fail "the certificate is team ${BASH_REMATCH[1]}'s, expected $team_given"
  [ -f "$app/embedded.mobileprovision" ] || fail "no provisioning profile in the bundle"
  security cms -D -i "$app/embedded.mobileprovision" >"$scratch/profile.plist" 2>/dev/null \
    || fail "the provisioning profile cannot be read"
  python3 -I -c '
import datetime, plistlib, sys
team, app_id = sys.argv[2], sys.argv[3]
with open(sys.argv[1], "rb") as f:
    profile = plistlib.load(f)
def bad(text):
    sys.exit("the provisioning profile " + text)
if not isinstance(profile, dict):
    bad("is not a dictionary")
if profile.get("TeamIdentifier") != [team]:
    bad("is not team %s alone: %r" % (team, profile.get("TeamIdentifier")))
entitlements = profile.get("Entitlements")
if not isinstance(entitlements, dict) or entitlements.get("application-identifier") != app_id:
    bad("is not for %s" % app_id)
if "ProvisionedDevices" in profile:
    bad("lists devices: it is not an App Store profile")
if profile.get("ProvisionsAllDevices"):
    bad("provisions all devices: it is an enterprise profile")
expires = profile.get("ExpirationDate")
if not isinstance(expires, datetime.datetime):
    bad("has no expiration date")
if expires.tzinfo is None:
    expires = expires.replace(tzinfo=datetime.timezone.utc)
if expires <= datetime.datetime.now(datetime.timezone.utc):
    bad("expired on %s" % expires.isoformat())
' "$scratch/profile.plist" "$team_given" "$team_given.$bundle_id" \
    || fail "the provisioning profile is not an App Store profile for this application"
fi

echo "check_signed: $app is signed for $team.$bundle_id with no capability beyond signing's own"
