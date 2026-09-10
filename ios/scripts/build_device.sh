#!/bin/bash
# Build the arm64 iPhone product bundle, optionally signed for a real device.
#
#   bash ios/scripts/build_device.sh                 # unsigned archive (Sideloadly)
#   FLYNES_IOS_SIGNING_TEAM=ABCDE12345 bash ios/scripts/build_device.sh
#
# The team id comes from the operator's environment. No Apple credential, profile,
# or .p12 is stored in this repository.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
cmake_bin="${CMAKE_BIN:-$HOME/Developer/FlyNES-tools/cmake-3.31.8-macos-universal/CMake.app/Contents/bin/cmake}"
build_dir="${FLYNES_BUILD_DIR:-$repo_dir/build/ios-device}"
config="${FLYNES_CONFIG:-Release}"
signing_team="${FLYNES_IOS_SIGNING_TEAM:-}"

test -x "$cmake_bin"
device_sdk="$(xcrun --sdk iphoneos --show-sdk-path)"
sdk_version="$(xcrun --sdk iphoneos --show-sdk-version)"
printf 'iphoneos sdk=%s (%s)\n' "$device_sdk" "$sdk_version"

"$cmake_bin" -S "$repo_dir/ios/app" -B "$build_dir" -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT="$device_sdk" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="${FLYNES_DEPLOYMENT_TARGET:-16.4}" \
  -DFLYNES_IOS_SIGNING_TEAM="$signing_team"

"$cmake_bin" --build "$build_dir" --config "$config" --target FlyNES --parallel 2

app_path="$build_dir/$config-iphoneos/FlyNES.app"
test -f "$app_path/FlyNES"
test -f "$app_path/default.metallib"
test -f "$app_path/en.lproj/Localizable.strings"
test -f "$app_path/zh-Hans.lproj/Localizable.strings"
architectures="$(xcrun lipo -archs "$app_path/FlyNES")"
test "$architectures" = "arm64"

if [ -n "$signing_team" ]; then
  # A signed bundle must carry a provisioning profile and a valid signature.
  test -f "$app_path/embedded.mobileprovision"
  codesign --verify --deep --strict "$app_path"
  echo "Signed device bundle for team $signing_team"
else
  # An unsigned archive must carry no profile and no entitlements.
  test ! -f "$app_path/embedded.mobileprovision"
  echo "Unsigned device archive (Sideloadly path)"
fi
printf 'architectures=%s\n' "$architectures"
printf '%s\n' "$app_path"
