#!/bin/bash
# Runs tools/ios/check_signed.sh against stand-ins for codesign, security,
# ditto and uname, so that every way it can refuse a build is tried on any
# system, without a certificate. Each case says what the stand-ins report
# and whether the check must pass, or must fail with a given message.
# Usage: CheckSignedStubs.sh <repository>
set -euo pipefail
repo=$(cd "$1" && pwd)
check=$repo/tools/ios/check_signed.sh
work=$(mktemp -d "${TMPDIR:-/tmp}/check_signed_stubs.XXXXXX")
trap 'rm -rf "$work"' EXIT
team=ABCDE12345
bundle=io.github.rmerck.multiballacademy

mkdir -p "$work/bin"
cat >"$work/bin/uname" <<'S'
#!/bin/sh
echo Darwin
S
cat >"$work/bin/codesign" <<'S'
#!/bin/bash
case "$*" in
  *--verify*) [ "${CS_VERIFY_FAIL:-0}" = 1 ] && { echo "a sealed resource is missing or invalid" >&2; exit 1; }; exit 0 ;;
  *--entitlements*) cat "$CS_ENT"; exit 0 ;;
  *-dvv*) echo "Executable=x"; echo "Authority=$CS_AUTH"; echo "Authority=Apple Worldwide Developer Relations Certification Authority"; exit 0 ;;
esac
exit 2
S
cat >"$work/bin/security" <<'S'
#!/bin/sh
[ "${SEC_FAIL:-0}" = 1 ] && exit 1
cat "$PROFILE"
S
cat >"$work/bin/ditto" <<'S'
#!/bin/sh
# ditto -x -k <zip> <directory>
exec python3 -I -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$3" "$4"
S
chmod +x "$work/bin/"*
printf "PINBALL_BUNDLE_ID='%s'\n" "$bundle" >"$work/product.env"

# plist <file> <python dict expression>: writes a property list.
plist() {
  python3 -I -c 'import datetime, plistlib, sys
now = datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None)
with open(sys.argv[1], "wb") as f:
    plistlib.dump(eval(sys.argv[2], {"now": now, "days": datetime.timedelta}), f)' "$1" "$2"
}
good_ent="{'application-identifier': '$team.$bundle', 'com.apple.developer.team-identifier': '$team', 'keychain-access-groups': ['$team.$bundle'], 'get-task-allow': False, 'beta-reports-active': True}"
good_profile="{'TeamIdentifier': ['$team'], 'Entitlements': {'application-identifier': '$team.$bundle'}, 'ExpirationDate': now + days(days=300)}"
good_auth="Apple Distribution: Some Developer ($team)"

app="$work/Multiball Academy.app"
passed=0
# run <name> <expect: pass | message> <entitlements> <profile> <authority> [options...]
run() {
  local name=$1 expect=$2 ent=$3 profile=$4 auth=$5
  shift 5
  rm -rf "$app" && mkdir -p "$app" && : >"$app/embedded.mobileprovision"
  plist "$work/ent.plist" "$ent"
  plist "$work/profile.plist" "$profile"
  local target=$app
  local output status=0
  if [ "${MAKE_IPA:-0}" = 1 ]; then
    (cd "$work" && rm -rf Payload x.ipa && mkdir Payload && cp -R "$app" Payload/ \
      && python3 -I -c 'import os, sys, zipfile
z = zipfile.ZipFile(sys.argv[1], "w")
for root, dirs, files in os.walk("Payload"):
    for name in dirs + files:
        z.write(os.path.join(root, name))' x.ipa)
    target=$work/x.ipa
  fi
  [ "${ADD_FRAMEWORKS:-0}" = 1 ] && mkdir -p "$app/Frameworks"
  [ "${NO_PROFILE:-0}" = 1 ] && rm -f "$app/embedded.mobileprovision"
  output=$(PATH="$work/bin:$PATH" CS_ENT="$work/ent.plist" CS_AUTH="$auth" PROFILE="$work/profile.plist" \
    PINBALL_IOS_TEAM_ID= bash "$check" "$target" "$work/product.env" "$@" 2>&1) || status=$?
  if [ "$expect" = pass ]; then
    [ "$status" = 0 ] || { echo "FAIL $name: expected a pass, got: $output"; exit 1; }
  else
    [ "$status" != 0 ] && grep -q -F -- "$expect" <<<"$output" \
      || { echo "FAIL $name: expected a refusal with '$expect', got ($status): $output"; exit 1; }
  fi
  echo "ok $name"
  passed=$((passed + 1))
}
ent_with() { # <python edit of dict e>
  python3 -I -c 'import sys
e = eval(sys.argv[1]); exec(sys.argv[2]); print(repr(e))' "$good_ent" "$1"
}
profile_with() {
  python3 -I -c 'import sys
p = sys.argv[1]; print(p[:-1] + ", " + sys.argv[2] + "}" if sys.argv[2] else p)' "$good_profile" "$1"
}

run development-build pass "$(ent_with "e['get-task-allow'] = True; del e['beta-reports-active']")" "$good_profile" "Apple Development: X ($team)"
run distribution-build pass "$good_ent" "$good_profile" "$good_auth" --distribution --team "$team"
MAKE_IPA=1 run distribution-ipa pass "$good_ent" "$good_profile" "$good_auth" --distribution --team "$team"
run distribution-no-team "needs the team" "$good_ent" "$good_profile" "$good_auth" --distribution
run bad-team-argument "ten capital letters" "$good_ent" "$good_profile" "$good_auth" --team abc
run extra-capability "not one signing adds" "$(ent_with "e['com.apple.security.application-groups'] = ['group.x']")" "$good_profile" "$good_auth"
run key-characters "characters an entitlement never has" "$(ent_with "e['get-task-allow\\tx'] = True")" "$good_profile" "$good_auth"
run other-identifier "application-identifier is" "$(ent_with "e['application-identifier'] = '$team.com.example.other'")" "$good_profile" "$good_auth"
run other-team "signed for team" "$good_ent" "$good_profile" "$good_auth" --team ZZZZZ99999
run keychain-group "keychain-access-groups" "$(ent_with "e['keychain-access-groups'] = ['$team.*']")" "$good_profile" "$good_auth"
run get-task-allow-true "get-task-allow is not false" "$(ent_with "e['get-task-allow'] = True")" "$good_profile" "$good_auth" --distribution --team "$team"
run get-task-allow-absent "get-task-allow is not false" "$(ent_with "del e['get-task-allow']")" "$good_profile" "$good_auth" --distribution --team "$team"
run beta-reports-absent "beta-reports-active is not true" "$(ent_with "del e['beta-reports-active']")" "$good_profile" "$good_auth" --distribution --team "$team"
run development-certificate "Apple Distribution certificate" "$good_ent" "$good_profile" "Apple Development: X ($team)" --distribution --team "$team"
run certificate-other-team "certificate is team" "$good_ent" "$good_profile" "Apple Distribution: X (ZZZZZ99999)" --distribution --team "$team"
NO_PROFILE=1 run no-profile "no provisioning profile" "$good_ent" "$good_profile" "$good_auth" --distribution --team "$team"
SEC_FAIL=1 run unreadable-profile "cannot be read" "$good_ent" "$good_profile" "$good_auth" --distribution --team "$team"
run profile-other-team "is not team" "$good_ent" "$(sed "s/'TeamIdentifier': \['$team'\]/'TeamIdentifier': ['ZZZZZ99999']/" <<<"$good_profile")" "$good_auth" --distribution --team "$team"
run profile-other-identifier "is not for" "$good_ent" "$(sed "s/'application-identifier': '$team.$bundle'/'application-identifier': '$team.*'/" <<<"$good_profile")" "$good_auth" --distribution --team "$team"
run profile-devices "lists devices" "$good_ent" "$(profile_with "'ProvisionedDevices': ['00008030-0001']")" "$good_auth" --distribution --team "$team"
run profile-all-devices "provisions all devices" "$good_ent" "$(profile_with "'ProvisionsAllDevices': True")" "$good_auth" --distribution --team "$team"
run profile-expired "expired" "$good_ent" "$(sed "s/now + days(days=300)/now - days(days=1)/" <<<"$good_profile")" "$good_auth" --distribution --team "$team"
CS_VERIFY_FAIL=1 run broken-signature "does not verify" "$good_ent" "$good_profile" "$good_auth"
ADD_FRAMEWORKS=1 run framework-inside "holds Frameworks" "$good_ent" "$good_profile" "$good_auth"
echo "check_signed stand-in cases: $passed passed"
