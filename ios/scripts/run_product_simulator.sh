#!/bin/bash
# An explicit simulator UDID prevents installs into an unrelated booted device.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
device_id="${1:?Usage: run_product_simulator.sh SIMULATOR_UDID}"
build_dir="${FLYNES_BUILD_DIR:-$repo_dir/build/ios-simulator}"
app_path="$build_dir/Debug-iphonesimulator/FlyNES.app"
test -f "$app_path/FlyNES"
# bootstatus -b boots an unbooted simulator and waits for it to become usable.
xcrun simctl bootstatus "$device_id" -b
xcrun simctl install "$device_id" "$app_path"
xcrun simctl launch --terminate-running-process "$device_id" com.flynes.app
open -a "$DEVELOPER_DIR/Applications/Simulator.app" --args -CurrentDeviceUDID "$device_id"
