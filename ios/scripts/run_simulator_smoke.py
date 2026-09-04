#!/usr/bin/env python3
"""Install and run the Stage-1 app, then verify its in-container result JSON."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import stat
import subprocess
import sys
import time


EXPECTED_FULL_SHA256 = (
    "1A3AC4FAF4B35640505344059AE5D91DAE07CD47E1FB4D9D2A33C76391F1C555"
)
EXPECTED_CORE_SHA1 = "77C42676DB38D384C1D6B00090ADBC820BF70AB0"
MARKER = "FLYNES_IOS_SMOKE_PASS"
RESULT_NAME = "flynes-ios-stage1-result.json"
EXPECTED_FIELDS = frozenset(
    {
        "schema_version",
        "status",
        "exit_code",
        "frames_run",
        "audio_samples",
        "audio_samples_changed",
        "state_bytes",
        "input_generation",
        "input_monotonic_ns",
        "input_pad0",
        "input_pad1",
        "input_pad2",
        "input_pad3",
        "video_sequence",
        "video_monotonic_ns",
        "non_black_pixels",
        "catalog_generation",
        "catalog_count",
        "full_file_sha256",
        "core_cartridge_sha1",
        "report",
    }
)
MAX_RESULT_JSON_BYTES = 8 * 1024
MAX_REPORT_CHARACTERS = 1024
MAX_LAUNCH_OUTPUT_BYTES = 64 * 1024
MAX_AUDIO_SAMPLES = 60 * 1024
MAX_STATE_BYTES = 8 * 1024 * 1024
MAX_MONOTONIC_NS = 10**18
MAX_NON_BLACK_PIXELS = 256 * 240
INSTALL_TIMEOUT_SECONDS = 30.0
CONTAINER_TIMEOUT_SECONDS = 15.0
TERMINATE_TIMEOUT_SECONDS = 10.0


def checked_output(arguments: list[str], timeout_seconds: float) -> str:
    return subprocess.run(
        arguments,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout_seconds,
    ).stdout.strip()


def require_integer(result: dict[str, object], key: str) -> int:
    value = result[key]
    if not isinstance(value, int) or isinstance(value, bool):
        raise RuntimeError(f"result field {key} must be an integer")
    return value


def expected_report(result: dict[str, object]) -> str:
    return (
        f"{MARKER}"
        f" frames={result['frames_run']}"
        f" audio_samples={result['audio_samples']}"
        f" audio_samples_changed={result['audio_samples_changed']}"
        f" state_bytes={result['state_bytes']}"
        f" input_generation={result['input_generation']}"
        f" input_monotonic_ns={result['input_monotonic_ns']}"
        f" input_pad0={result['input_pad0']}"
        f" input_pad1={result['input_pad1']}"
        f" input_pad2={result['input_pad2']}"
        f" input_pad3={result['input_pad3']}"
        f" video_sequence={result['video_sequence']}"
        f" video_monotonic_ns={result['video_monotonic_ns']}"
        f" non_black_pixels={result['non_black_pixels']}"
        f" catalog_generation={result['catalog_generation']}"
        f" catalog_count={result['catalog_count']}"
        f" full_file_sha256={result['full_file_sha256']}"
        f" core_cartridge_sha1={result['core_cartridge_sha1']}"
    )


def validate_result(result: object, launch_output: str) -> None:
    if not isinstance(result, dict):
        raise RuntimeError("result JSON is not an object")
    if set(result) != EXPECTED_FIELDS:
        missing = sorted(EXPECTED_FIELDS - set(result))
        unknown = sorted(set(result) - EXPECTED_FIELDS)
        raise RuntimeError(
            f"result JSON schema mismatch: missing={missing!r} unknown={unknown!r}"
        )
    expected_integers = {
        "schema_version": 1,
        "exit_code": 0,
        "frames_run": 60,
        "input_generation": 4,
        "input_pad0": 1,
        "input_pad1": 2,
        "input_pad2": 4,
        "input_pad3": 8,
        "catalog_generation": 0,
        "catalog_count": 0,
    }
    for key, expected_value in expected_integers.items():
        value = require_integer(result, key)
        if value != expected_value:
            raise RuntimeError(f"unexpected integer result field {key}: {value!r}")
    expected_strings = {
        "status": "PASS",
        "full_file_sha256": EXPECTED_FULL_SHA256,
        "core_cartridge_sha1": EXPECTED_CORE_SHA1,
    }
    for key, expected_value in expected_strings.items():
        value = result[key]
        if not isinstance(value, str) or value != expected_value:
            raise RuntimeError(f"unexpected string result field {key}: {value!r}")

    audio_samples = require_integer(result, "audio_samples")
    audio_samples_changed = require_integer(result, "audio_samples_changed")
    state_bytes = require_integer(result, "state_bytes")
    input_monotonic_ns = require_integer(result, "input_monotonic_ns")
    video_sequence = require_integer(result, "video_sequence")
    video_monotonic_ns = require_integer(result, "video_monotonic_ns")
    non_black_pixels = require_integer(result, "non_black_pixels")
    if not 1 <= audio_samples <= MAX_AUDIO_SAMPLES:
        raise RuntimeError("audio sample count is outside the smoke buffer contract")
    if not 1 <= audio_samples_changed <= audio_samples:
        raise RuntimeError("audio output did not overwrite its reported sample range")
    if not 1 <= state_bytes <= MAX_STATE_BYTES:
        raise RuntimeError("state size is outside the shared ROM size contract")
    for key, value in (
        ("input_monotonic_ns", input_monotonic_ns),
        ("video_monotonic_ns", video_monotonic_ns),
    ):
        if not 1 <= value <= MAX_MONOTONIC_NS:
            raise RuntimeError(f"result field {key} is outside the monotonic clock bound")
    if video_sequence != require_integer(result, "frames_run"):
        raise RuntimeError("video sequence does not match the completed frame count")
    if not 1 <= non_black_pixels <= MAX_NON_BLACK_PIXELS:
        raise RuntimeError("non-black pixel count exceeds the 256x240 frame")

    report = result["report"]
    wanted_report = expected_report(result)
    if (
        not isinstance(report, str)
        or len(report) > MAX_REPORT_CHARACTERS
        or report != wanted_report
    ):
        raise RuntimeError("result report does not exactly match the structured fields")
    if len(launch_output.encode("utf-8")) > MAX_LAUNCH_OUTPUT_BYTES:
        raise RuntimeError("simulator stdout exceeds the evidence size bound")
    if launch_output.count(MARKER) != 1 or report not in launch_output.splitlines():
        raise RuntimeError("simulator stdout does not contain the exact PASS report once")


def load_result_json(path: Path) -> tuple[object, bytes]:
    try:
        metadata = path.lstat()
    except OSError as error:
        raise RuntimeError("could not stat app result JSON") from error
    if path.is_symlink() or not stat.S_ISREG(metadata.st_mode):
        raise RuntimeError("app result JSON is not a regular file")
    if metadata.st_size <= 0 or metadata.st_size > MAX_RESULT_JSON_BYTES:
        raise RuntimeError("app result JSON is outside the byte-size bound")
    try:
        with path.open("rb") as stream:
            payload = stream.read(MAX_RESULT_JSON_BYTES + 1)
    except OSError as error:
        raise RuntimeError("could not read app result JSON") from error
    if len(payload) != metadata.st_size or len(payload) > MAX_RESULT_JSON_BYTES:
        raise RuntimeError("app result JSON changed while being read or is oversized")

    def reject_duplicate_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
        decoded: dict[str, object] = {}
        for key, value in pairs:
            if key in decoded:
                raise ValueError(f"duplicate JSON key: {key}")
            decoded[key] = value
        return decoded

    try:
        text = payload.decode("utf-8", errors="strict")
        result = json.loads(text, object_pairs_hook=reject_duplicate_keys)
    except (UnicodeError, json.JSONDecodeError, ValueError) as error:
        raise RuntimeError("could not decode app result JSON") from error
    return result, payload


def bounded_launch_log(output: str) -> str:
    encoded = output.encode("utf-8")
    if len(encoded) <= MAX_LAUNCH_OUTPUT_BYTES:
        return output
    prefix = encoded[: MAX_LAUNCH_OUTPUT_BYTES - 32].decode("utf-8", errors="ignore")
    return prefix + "\n[stdout truncated by runner]\n"


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
        ["xcrun", "simctl", "install", args.udid, str(args.app)],
        check=True,
        timeout=min(INSTALL_TIMEOUT_SECONDS, args.timeout_seconds),
    )
    container_text = checked_output(
        ["xcrun", "simctl", "get_app_container", args.udid, args.bundle_id, "data"],
        min(CONTAINER_TIMEOUT_SECONDS, args.timeout_seconds),
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
        try:
            subprocess.run(
                ["xcrun", "simctl", "terminate", args.udid, args.bundle_id],
                check=False,
                timeout=TERMINATE_TIMEOUT_SECONDS,
            )
        except (OSError, subprocess.TimeoutExpired):
            pass
        launch.kill()
        trailing_output, _ = launch.communicate(timeout=TERMINATE_TIMEOUT_SECONDS)
        launch_output += trailing_output
        raise RuntimeError("simulator smoke did not exit before the timeout") from error
    finally:
        (args.evidence_dir / "simulator-launch.log").write_text(
            bounded_launch_log(launch_output), encoding="utf-8"
        )

    if launch.returncode != 0:
        raise RuntimeError(f"simctl launch returned {launch.returncode}")

    deadline = time.monotonic() + min(10.0, args.timeout_seconds)
    while not result_path.is_file() and time.monotonic() < deadline:
        time.sleep(0.1)
    if not result_path.is_file():
        raise RuntimeError("launched app exited without atomic result JSON")

    result, result_payload = load_result_json(result_path)
    validate_result(result, launch_output)
    # Publish only the exact bytes that passed the closed-schema validation.
    (args.evidence_dir / "simulator-result.json").write_bytes(result_payload)
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"run_simulator_smoke: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
