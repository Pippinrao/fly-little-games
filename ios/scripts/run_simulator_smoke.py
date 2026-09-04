#!/usr/bin/env python3
"""Install and run the Stage-1 app, then verify its in-container result JSON."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time


EXPECTED_FULL_SHA256 = (
    "1A3AC4FAF4B35640505344059AE5D91DAE07CD47E1FB4D9D2A33C76391F1C555"
)
EXPECTED_CORE_SHA1 = "77C42676DB38D384C1D6B00090ADBC820BF70AB0"
MARKER = "FLYNES_IOS_SMOKE_PASS"
RESULT_NAME = "flynes-ios-stage1-result.json"


def checked_output(arguments: list[str]) -> str:
    return subprocess.run(
        arguments,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    ).stdout.strip()


def validate_result(result: object, launch_output: str) -> None:
    if not isinstance(result, dict):
        raise RuntimeError("result JSON is not an object")
    expected_integers = {
        "schema_version": 1,
        "exit_code": 0,
        "frames_run": 60,
        "catalog_generation": 0,
        "catalog_count": 0,
    }
    for key, expected_value in expected_integers.items():
        value = result.get(key)
        if (
            not isinstance(value, int)
            or isinstance(value, bool)
            or value != expected_value
        ):
            raise RuntimeError(f"unexpected integer result field {key}: {value!r}")
    expected_strings = {
        "status": "PASS",
        "full_file_sha256": EXPECTED_FULL_SHA256,
        "core_cartridge_sha1": EXPECTED_CORE_SHA1,
    }
    for key, expected_value in expected_strings.items():
        value = result.get(key)
        if not isinstance(value, str) or value != expected_value:
            raise RuntimeError(f"unexpected string result field {key}: {value!r}")
    for key in (
        "audio_samples",
        "state_bytes",
        "input_generation",
        "input_monotonic_ns",
        "video_sequence",
        "video_monotonic_ns",
        "non_black_pixels",
    ):
        value = result.get(key)
        if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
            raise RuntimeError(f"result field {key} must be a positive integer")
    report = result.get("report")
    if not isinstance(report, str) or report.count(MARKER) != 1:
        raise RuntimeError("result report does not contain exactly one PASS marker")
    if launch_output.count(MARKER) != 1:
        raise RuntimeError("simulator stdout does not contain exactly one PASS marker")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--udid", required=True)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--bundle-id", required=True)
    parser.add_argument("--evidence-dir", required=True, type=Path)
    parser.add_argument("--timeout-seconds", type=float, default=60.0)
    args = parser.parse_args()

    if args.timeout_seconds <= 0.0 or args.timeout_seconds > 60.0:
        parser.error("--timeout-seconds must be in (0, 60]")
    if not args.app.is_dir():
        raise RuntimeError(f"app bundle does not exist: {args.app}")
    args.evidence_dir.mkdir(parents=True, exist_ok=True)

    # Exact commands are kept visible for the repository's static policy gate:
    # simctl install; simctl get_app_container; simctl launch --console.
    subprocess.run(
        ["xcrun", "simctl", "install", args.udid, str(args.app)], check=True
    )
    container_text = checked_output(
        ["xcrun", "simctl", "get_app_container", args.udid, args.bundle_id, "data"]
    )
    container = Path(container_text)
    if not container.is_absolute() or not container.is_dir():
        raise RuntimeError(f"simctl returned an invalid data container: {container_text!r}")
    result_path = container / "Documents" / RESULT_NAME
    if result_path.exists():
        result_path.unlink()

    launch = subprocess.Popen(
        [
            "xcrun",
            "simctl",
            "launch",
            "--console",
            args.udid,
            args.bundle_id,
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    launch_output = ""
    try:
        launch_output, _ = launch.communicate(timeout=args.timeout_seconds)
    except subprocess.TimeoutExpired as error:
        subprocess.run(
            ["xcrun", "simctl", "terminate", args.udid, args.bundle_id], check=False
        )
        launch.kill()
        trailing_output, _ = launch.communicate()
        launch_output += trailing_output
        raise RuntimeError("simulator smoke did not exit before the timeout") from error
    finally:
        (args.evidence_dir / "simulator-launch.log").write_text(
            launch_output, encoding="utf-8"
        )

    if launch.returncode != 0:
        raise RuntimeError(f"simctl launch returned {launch.returncode}")

    deadline = time.monotonic() + min(10.0, args.timeout_seconds)
    while not result_path.is_file() and time.monotonic() < deadline:
        time.sleep(0.1)
    if not result_path.is_file():
        raise RuntimeError("launched app exited without atomic result JSON")

    try:
        result = json.loads(result_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise RuntimeError("could not decode app result JSON") from error
    validate_result(result, launch_output)
    shutil.copyfile(result_path, args.evidence_dir / "simulator-result.json")
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"run_simulator_smoke: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
