#!/bin/bash
# Package the built arm64 device bundle for iPhone acceptance.
#
#   bash ios/scripts/package_device.sh
#
# Writes an unsigned IPA (the Sideloadly path) plus a SHA-256 into
# build/ios-device/evidence/. No Apple credential is involved, and no ROM other
# than the repository's own licensed fixture is added.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
build_dir="${FLYNES_BUILD_DIR:-$repo_dir/build/ios-device}"
config="${FLYNES_CONFIG:-Release}"
bundle_id="com.flynes.app"
evidence_dir="$build_dir/evidence"
app_path="$build_dir/$config-iphoneos/FlyNES.app"

test -f "$app_path/FlyNES"
test -f "$app_path/default.metallib"
test "$(xcrun lipo -archs "$app_path/FlyNES")" = "arm64"
test -f "$app_path/Info.plist"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app_path/Info.plist")" = "$bundle_id"
test ! -f "$app_path/embedded.mobileprovision"
test "$(find "$app_path" -name '*.appex' | wc -l | tr -d ' ')" = "0"

mkdir -p "$evidence_dir"
rm -rf "$evidence_dir/Payload"
mkdir -p "$evidence_dir/Payload"
cp -R "$app_path" "$evidence_dir/Payload/FlyNES.app"
(cd "$evidence_dir" && zip -qr FlyNES-unsigned.ipa Payload)
shasum -a 256 "$evidence_dir/FlyNES-unsigned.ipa" | tee "$evidence_dir/FlyNES-unsigned.ipa.sha256"
/usr/libexec/PlistBuddy -c 'Print :MinimumOSVersion' "$app_path/Info.plist" \
  | tee "$evidence_dir/minimum-os.txt"
printf '%s\n' "$evidence_dir/FlyNES-unsigned.ipa"
