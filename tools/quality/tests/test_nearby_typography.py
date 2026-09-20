"""Failing fixtures for the nearby typography freeze (TYPO-00.S)."""

from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

QUALITY = Path(__file__).resolve().parents[1]
REPO_ROOT = Path(__file__).resolve().parents[3]
if str(QUALITY) not in sys.path:
    sys.path.insert(0, str(QUALITY))

import check_nearby_typography as checker  # noqa: E402

REQUIRED_ROLES = (
    "pageTitle",
    "paneTitle",
    "sectionTitle",
    "body",
    "muted",
    "action",
    "primaryAction",
    "kicker",
    "inviteCode",
    "codeInput",
)


def _role(size, weight, line_height, tracking, selector, nodes, unit="css-px"):
    return {
        "size": size,
        "weight": weight,
        "lineHeight": line_height,
        "tracking": tracking,
        "unit": unit,
        "selector": selector,
        "nodes": nodes,
    }


def approved_contract():
    return {
        "contractId": "flynes-nearby-typography-v1",
        "units": {
            "size": "css-px-at-default-scale",
            "lineHeight": "css-px-line-box",
            "tracking": "em",
            "notTheSamePhysicalUnit": [
                "css-px",
                "android-sp",
                "harmony-fp",
                "ios-pt",
            ],
        },
        "roles": {
            "pageTitle": _role(
                18, 600, 23.4, 0,
                ["#fly-existing h2", ".fn-header h2"],
                ["Nearby h2"],
            ),
            "paneTitle": _role(
                21, 600, 27.3, 0,
                "#fly-existing h3",
                ["left pane h3"],
            ),
            "sectionTitle": _role(
                15, 600, 21.75, 0,
                "#fly-existing h4",
                ["h4"],
            ),
            "body": _role(
                14, 400, 20.3, 0,
                "#fly-existing",
                ["ordinary labels/body"],
            ),
            "muted": _role(
                12, 400, 17.4, 0,
                ".fe-muted",
                ["helper copy", "footer", "reason"],
            ),
            "action": _role(
                14, 400, 20.3, 0,
                [".fe-button", ".fn-tab"],
                ["ordinary buttons/tabs"],
            ),
            "primaryAction": _role(
                14, 600, 20.3, 0,
                ".fe-launch",
                ["primary buttons"],
            ),
            "kicker": _role(
                11, 400, 15.95, ".13em",
                ".fn-kicker",
                ["small labels above pane title"],
            ),
            "inviteCode": _role(
                29, 600, 40.6, ".17em",
                ".fn-code",
                ["invite code"],
            ),
            "codeInput": _role(
                28, 400, 40.6, ".16em",
                ".fn-code-input",
                ["join-code input"],
            ),
        },
        "overrides": {
            "#fly-existing .fe-detail-title h3": {"fontSize": 20},
            "#fly-existing .fe-filter": {"fontSize": 13},
            "#fly-existing .fe-game-copy strong": {"fontSize": 14, "weight": 600},
            "#fly-existing .fe-game-copy small": {"fontSize": 11},
            "#fly-existing .fe-thumb": {"fontSize": 11, "weight": 600},
            "#fly-existing .fe-count": {"fontSize": 12},
            "#fly-existing .fe-list-hint": {"fontSize": 11},
            "#fly-existing .fe-search input": {"fontSize": 16},
        },
    }


def copied_constant_measurement():
    return {
        "elementId": "nearby_entry_subtitle",
        "role": "muted",
        "baseFontSize": 12,
        "effectiveFontSizeLogical": 12,
        "weight": 400,
        "scaleMode": "system",
        "requestedFontScale": 1,
        "appliedFontScale": 1,
        "clipped": False,
    }


class NearbyTypographyTests(unittest.TestCase):
    def _run_root(self, root: Path):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result = checker.main(["check_nearby_typography.py", str(root)])
        return result, output.getvalue()

    def _check_contract(self, contract: dict):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "shared" / "schema" / "nearby_typography_v1.json"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(contract), encoding="utf-8")
            return self._run_root(root)

    def test_muted_subtitle_size_14_fails(self):
        contract = approved_contract()
        contract["roles"]["muted"]["size"] = 14
        result, output = self._check_contract(contract)
        self.assertEqual(1, result, output)
        self.assertIn("FAIL:", output)
        self.assertRegex(output, r"muted|subtitle")
        self.assertRegex(output, r"14")
        self.assertRegex(output, r"12")

    def test_pane_title_headline_size_24_fails(self):
        contract = approved_contract()
        contract["roles"]["paneTitle"]["size"] = 24
        result, output = self._check_contract(contract)
        self.assertEqual(1, result, output)
        self.assertIn("FAIL:", output)
        self.assertRegex(output, r"paneTitle|headline")
        self.assertRegex(output, r"24")
        self.assertRegex(output, r"21")

    def test_action_button_size_16_fails(self):
        contract = approved_contract()
        contract["roles"]["action"]["size"] = 16
        result, output = self._check_contract(contract)
        self.assertEqual(1, result, output)
        self.assertIn("FAIL:", output)
        self.assertRegex(output, r"action|button")
        self.assertRegex(output, r"16")
        self.assertRegex(output, r"14")

    def test_invite_code_tracking_12_fails(self):
        for tracking in (".12", ".12em"):
            with self.subTest(tracking=tracking):
                contract = approved_contract()
                contract["roles"]["inviteCode"]["tracking"] = tracking
                result, output = self._check_contract(contract)
                self.assertEqual(1, result, output)
                self.assertIn("FAIL:", output)
                self.assertIn("inviteCode", output)
                self.assertRegex(output, r"\.12|0\.12")
                self.assertRegex(output, r"\.17em|0\.17")

    def test_typography_file_missing_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            result, output = self._run_root(Path(directory))
        self.assertEqual(1, result, output)
        self.assertIn("FAIL:", output)
        self.assertRegex(output, r"nearby_typography_v1\.json|missing")

    def test_copied_json_constants_are_not_native_measurements(self):
        record = copied_constant_measurement()
        problems = checker.Problems()
        checker.check_measurement(record, problems)
        self.assertTrue(problems.items)
        text = " ".join(problems.items).lower()
        self.assertTrue(
            any(token in text for token in (
                "platform", "bounds", "screenshot", "fingerprint", "native",
            )),
            text,
        )

    def test_approved_contract_passes(self):
        result, output = self._check_contract(approved_contract())
        self.assertEqual(0, result, output)
        self.assertIn("OK:", output)

    def test_frozen_repo_file_passes(self):
        result, output = self._run_root(REPO_ROOT)
        self.assertEqual(0, result, output)
        self.assertIn("OK:", output)


if __name__ == "__main__":
    unittest.main()
