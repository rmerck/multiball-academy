#!/bin/sh
# Notarises a built and signed "Space Cadet Pinball.app" with Apple, staples
# the ticket to it and checks the result.
#
# Usage: tools/notarize.sh <path/to/App.app> <keychain-profile>
#
# The app must have been built with -DPINBALL_CODESIGN_IDENTITY set to a
# Developer ID Application certificate (hardened runtime, secure timestamp).
# <keychain-profile> is the name of credentials stored beforehand with
#   xcrun notarytool store-credentials <keychain-profile>
# so that no password or key ever appears here or on the command line.
#
# Nothing may change inside the bundle afterwards (a rebuild signs it again
# and drops the ticket): notarise last, then distribute.
set -eu

fail() {
  echo "notarize: $*" >&2
  exit 1
}

[ "$#" -eq 2 ] || fail "usage: $0 <path/to/App.app> <keychain-profile>"
app=$1
profile=$2

[ -n "$profile" ] || fail "the keychain profile name is empty"
case $profile in
  -*) fail "the keychain profile name must not start with a dash: $profile" ;;
esac
case $app in
  *.app | *.app/) ;;
  *) fail "not an application bundle (.app): $app" ;;
esac
[ -d "$app" ] || fail "no such directory: $app"
[ ! -L "${app%/}" ] || fail "refusing a symbolic link: $app"
# An absolute path, so a name that starts with a dash cannot pass as an option.
app=$(cd "$app" && pwd -P)
[ -f "$app/Contents/Info.plist" ] || fail "no Contents/Info.plist in $app"

[ "$(uname -s)" = Darwin ] || fail "macOS only"
for tool in xcrun codesign ditto spctl; do
  command -v "$tool" >/dev/null 2>&1 || fail "$tool not found"
done

echo "notarize: checking the signature of $app"
codesign --verify --deep --strict --verbose=2 "$app" \
  || fail "the signature does not verify; build the app again"
details=$(codesign --display --verbose=4 "$app" 2>&1) \
  || fail "cannot read the signature"
printf '%s\n' "$details" | grep -q '^Authority=Developer ID Application:' \
  || fail "not signed with a Developer ID Application certificate (configure with -DPINBALL_CODESIGN_IDENTITY=...)"
printf '%s\n' "$details" | grep -Eq 'flags=.*runtime' \
  || fail "the hardened runtime is not on"
printf '%s\n' "$details" | grep -q '^Timestamp=' \
  || fail "the signature has no secure timestamp"

work=$(mktemp -d "${TMPDIR:-/tmp}/notarize.XXXXXX")
trap 'rm -rf "$work"' EXIT
archive="$work/$(basename "$app" .app).zip"

echo "notarize: zipping"
ditto -c -k --keepParent "$app" "$archive"

echo "notarize: submitting and waiting for Apple's answer"
xcrun notarytool submit "$archive" --keychain-profile "$profile" --wait \
  --output-format json >"$work/result.json" \
  || { cat "$work/result.json" >&2 2>/dev/null; fail "notarytool submit failed"; }
cat "$work/result.json"
echo
# --wait exits 0 for a rejected submission too: the status is what counts.
grep -Eq '"status"[[:space:]]*:[[:space:]]*"Accepted"' "$work/result.json" \
  || fail "the submission was not accepted (see: xcrun notarytool log <id> --keychain-profile <profile>)"

echo "notarize: stapling the ticket"
xcrun stapler staple "$app" || fail "stapling failed"
xcrun stapler validate "$app" || fail "the stapled ticket does not validate"

echo "notarize: verifying"
codesign --verify --deep --strict --verbose=2 "$app" \
  || fail "the signature does not verify after stapling"
spctl -a -vv "$app" || fail "Gatekeeper does not accept the app"

echo "notarize: done: $app"
