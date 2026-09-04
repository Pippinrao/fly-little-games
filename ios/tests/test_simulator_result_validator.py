#!/usr/bin/env python3
"""Host tests for the structured simulator completion evidence."""

from copy import deepcopy
from pathlib import Path
import importlib.util
import sys


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "run_simulator_smoke.py"
SPEC = importlib.util.spec_from_file_location("run_simulator_smoke", SCRIPT)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load simulator runner")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def expect_rejected(result: dict[str, object], output: str) -> None:
    try:
        MODULE.validate_result(result, output)
    except RuntimeError:
        return
    raise AssertionError("invalid simulator evidence was accepted")


def main() -> int:
    valid: dict[str, object] = {
        "schema_version": 1,
        "status": "PASS",
        "exit_code": 0,
        "frames_run": 60,
        "audio_samples": 47921,
        "state_bytes": 2023,
        "input_generation": 1,
        "input_monotonic_ns": 1,
        "video_sequence": 60,
        "video_monotonic_ns": 2,
        "non_black_pixels": 4013,
        "catalog_generation": 0,
        "catalog_count": 0,
        "full_file_sha256": MODULE.EXPECTED_FULL_SHA256,
        "core_cartridge_sha1": MODULE.EXPECTED_CORE_SHA1,
        "report": MODULE.MARKER,
    }
    MODULE.validate_result(valid, MODULE.MARKER + "\n")

    for key, bad_value in (
        ("schema_version", True),
        ("status", "FAIL"),
        ("exit_code", False),
        ("exit_code", 1),
        ("frames_run", 59),
        ("audio_samples", 0),
        ("input_generation", True),
        ("non_black_pixels", -1),
        ("catalog_generation", False),
        ("full_file_sha256", "0" * 64),
    ):
        invalid = deepcopy(valid)
        invalid[key] = bad_value
        expect_rejected(invalid, MODULE.MARKER + "\n")
    expect_rejected(valid, "")
    expect_rejected(valid, MODULE.MARKER + MODULE.MARKER)

    print("flynes_ios_simulator_result_validator: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"flynes_ios_simulator_result_validator: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
