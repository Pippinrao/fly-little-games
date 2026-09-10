#!/usr/bin/env python3
"""Host tests for the structured simulator completion evidence."""

from copy import deepcopy
from pathlib import Path
import importlib.util
import json
import os
import re
import subprocess
import sys
import tempfile
import time


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


def load_script(name: str, path: Path) -> object:
    if not path.is_file():
        raise AssertionError(f"missing Stage-1 script: {path.name}")
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"could not load Stage-1 script: {path.name}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def expected_report(result: dict[str, object]) -> str:
    return (
        f"{MODULE.MARKER}"
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


def expect_artifact_rejected(module: object, root: Path) -> None:
    try:
        module.validate_evidence_directory(root, require_complete=False)
    except RuntimeError:
        return
    raise AssertionError("unsafe Stage-1 artifact inventory was accepted")


def main() -> int:
    valid: dict[str, object] = {
        "schema_version": 1,
        "status": "PASS",
        "exit_code": 0,
        "frames_run": 60,
        "audio_samples": 47921,
        "audio_samples_changed": 47921,
        "state_bytes": 2023,
        "input_generation": 4,
        "input_monotonic_ns": 1,
        "input_pad0": 1,
        "input_pad1": 2,
        "input_pad2": 4,
        "input_pad3": 8,
        "video_sequence": 60,
        "video_monotonic_ns": 2,
        "non_black_pixels": 4013,
        "catalog_generation": 0,
        "catalog_count": 0,
        "full_file_sha256": MODULE.EXPECTED_FULL_SHA256,
        "core_cartridge_sha1": MODULE.EXPECTED_CORE_SHA1,
        "report": "",
    }
    valid["report"] = expected_report(valid)
    launch_output = "UIKit startup\n" + str(valid["report"]) + "\n"
    MODULE.validate_result(valid, launch_output)

    for key, bad_value in (
        ("schema_version", True),
        ("status", "FAIL"),
        ("exit_code", False),
        ("exit_code", 1),
        ("frames_run", 59),
        ("audio_samples", 0),
        ("audio_samples", 60 * 1024 + 1),
        ("audio_samples_changed", 0),
        ("audio_samples_changed", 47922),
        ("state_bytes", 8 * 1024 * 1024 + 1),
        ("input_generation", True),
        ("input_generation", 1),
        ("input_monotonic_ns", 10**18 + 1),
        ("input_pad0", 2),
        ("input_pad1", 1),
        ("input_pad2", 8),
        ("input_pad3", 4),
        ("video_sequence", 59),
        ("video_monotonic_ns", 10**18 + 1),
        ("non_black_pixels", -1),
        ("non_black_pixels", 256 * 240 + 1),
        ("catalog_generation", False),
        ("full_file_sha256", "0" * 64),
    ):
        invalid = deepcopy(valid)
        invalid[key] = bad_value
        invalid["report"] = expected_report(invalid)
        expect_rejected(invalid, str(invalid["report"]) + "\n")

    missing = deepcopy(valid)
    del missing["state_bytes"]
    expect_rejected(missing, launch_output)
    unknown = deepcopy(valid)
    unknown["unexpected"] = 1
    expect_rejected(unknown, launch_output)
    bad_report = deepcopy(valid)
    bad_report["report"] = str(valid["report"]) + " trailing"
    expect_rejected(bad_report, str(bad_report["report"]) + "\n")
    oversized_report = deepcopy(valid)
    oversized_report["report"] = MODULE.MARKER + "x" * MODULE.MAX_REPORT_CHARACTERS
    expect_rejected(oversized_report, str(oversized_report["report"]) + "\n")
    expect_rejected(valid, "")
    expect_rejected(valid, MODULE.MARKER + MODULE.MARKER)
    expect_rejected(valid, "x" * (64 * 1024 + 1) + launch_output)

    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        result_path = root / MODULE.RESULT_NAME
        payload = json.dumps(valid, sort_keys=True, separators=(",", ":")).encode("utf-8")
        result_path.write_bytes(payload)
        loaded, loaded_payload = MODULE.load_result_json(result_path)
        if loaded != valid or loaded_payload != payload:
            raise AssertionError("bounded result loader changed valid evidence")
        result_path.write_bytes(b"{" + b" " * MODULE.MAX_RESULT_JSON_BYTES + b"}")
        try:
            MODULE.load_result_json(result_path)
        except RuntimeError:
            pass
        else:
            raise AssertionError("oversized result JSON was accepted")
        result_path.write_text('{"schema_version":1,"schema_version":1}', encoding="utf-8")
        try:
            MODULE.load_result_json(result_path)
        except RuntimeError:
            pass
        else:
            raise AssertionError("duplicate result JSON keys were accepted")

    scripts = SCRIPT.parent
    bounded_script = scripts / "run_with_timeout.py"
    success = subprocess.run(
        [
            sys.executable,
            str(bounded_script),
            "--timeout-seconds",
            "2",
            "--",
            sys.executable,
            "-c",
            "print('bounded-ok')",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if success.returncode != 0 or success.stdout.strip() != "bounded-ok":
        raise AssertionError("bounded command wrapper did not preserve success output")
    started = time.monotonic()
    timed_out = subprocess.run(
        [
            sys.executable,
            str(bounded_script),
            "--timeout-seconds",
            "0.1",
            "--",
            sys.executable,
            "-c",
            "import time; time.sleep(5)",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if timed_out.returncode != 124 or time.monotonic() - started > 3.0:
        raise AssertionError("bounded command wrapper did not terminate on time")

    artifact_module = load_script(
        "validate_evidence_artifact", scripts / "validate_evidence_artifact.py"
    )
    workflow = (SCRIPT.parents[2] / ".github" / "workflows" / "ios-stage1.yml").read_text(
        encoding="utf-8"
    )
    uploaded = set(re.findall(r"artifacts/ios-stage1/([A-Za-z0-9.-]+)", workflow))
    if uploaded != artifact_module.REQUIRED_SUCCESS_FILES:
        raise AssertionError("artifact action paths differ from the validated allowlist")
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        allowed = root / "host-contract.txt"
        allowed.write_text("contract PASS\n", encoding="utf-8")
        entries = artifact_module.validate_evidence_directory(
            root, require_complete=False
        )
        if [entry.name for entry in entries] != [allowed.name]:
            raise AssertionError("artifact inventory omitted an allowed text file")

        hardlink = root / "cmake-version.txt"
        os.link(allowed, hardlink)
        expect_artifact_rejected(artifact_module, root)
        hardlink.unlink()
        (root / "from_below.nes").write_bytes(b"NES\x1a")
        expect_artifact_rejected(artifact_module, root)
        (root / "from_below.nes").unlink()
        (root / "unknown.txt").write_text("unexpected\n", encoding="utf-8")
        expect_artifact_rejected(artifact_module, root)
        (root / "unknown.txt").unlink()
        allowed.write_bytes(b"text\x00binary")
        expect_artifact_rejected(artifact_module, root)
        allowed.write_text("x", encoding="utf-8")
        (root / "nested").mkdir()
        expect_artifact_rejected(artifact_module, root)
        (root / "nested").rmdir()
        allowed.write_bytes(b"x" * (artifact_module.MAX_FILE_BYTES + 1))
        expect_artifact_rejected(artifact_module, root)

    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        for name in artifact_module.REQUIRED_SUCCESS_FILES:
            content = "{}\n" if name.endswith(".json") else "evidence\n"
            (root / name).write_text(content, encoding="utf-8")
        artifact_module.validate_evidence_directory(root, require_complete=True)

    print("flynes_ios_simulator_result_validator: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"flynes_ios_simulator_result_validator: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
