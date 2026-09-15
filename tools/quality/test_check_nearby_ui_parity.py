import contextlib
import importlib.util
import io
import json
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("check_nearby_ui_parity.py")
SPEC = importlib.util.spec_from_file_location("check_nearby_ui_parity", SCRIPT)
CHECKER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(CHECKER)


class NearbyUiParityCheckerTests(unittest.TestCase):
    def _base_record(self, case_id="C01-idle"):
        return {
            "caseId": case_id,
            "locale": "zh-CN",
            "contentSize": [736, 414],
            "fontScale": 1.0,
            "screenId": "G00",
            "surfaceId": "G00",
            "orientation": "landscape",
            "safeAreaVariant": "none",
            "entryText": "附近联机",
            "category": "ALL",
            "multiplayerOnly": False,
            "visibleIds": [],
            "enabledActions": [],
            "reasons": [],
            "containerOrder": ["topBar", "content"],
            "overflowOutsideGameGrid": False,
            "bounds": {"topBar": [0, 0, 736, 64]},
            "safeInsets": [0, 0, 0, 0],
            "keyboardVisible": False,
            "screenshotPath": "missing.png",
            "evidenceKind": "html",
        }

    def _run(self, records_by_platform):
        with tempfile.TemporaryDirectory() as directory:
            evidence = Path(directory)
            for platform, records in records_by_platform.items():
                (evidence / f"nearby-parity-{platform}.json").write_text(
                    json.dumps(records), encoding="utf-8")
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = CHECKER.main([str(SCRIPT), str(evidence)])
            return result, output.getvalue()

    def test_rejects_three_platform_c01_only_without_native_layout_evidence(self):
        record = {
            "caseId": "C01-idle",
            "locale": "zh-CN",
            "screenId": "G00",
            "multiplayerOnly": False,
            "visibleIds": [],
            "enabledActions": [],
            "reasons": [],
            "containerOrder": [],
            "overflowOutsideGameGrid": False,
        }
        with tempfile.TemporaryDirectory() as directory:
            evidence = Path(directory)
            for platform in CHECKER.PLATFORMS:
                payload = dict(record, platform=platform)
                (evidence / f"nearby-parity-{platform}.json").write_text(
                    json.dumps([payload]), encoding="utf-8")

            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = CHECKER.main([str(SCRIPT), str(evidence)])

        self.assertEqual(1, result, output.getvalue())
        self.assertIn("C02", output.getvalue())
        self.assertIn("contentSize", output.getvalue())
        self.assertIn("fontScale", output.getvalue())
        self.assertIn("bounds", output.getvalue())
        self.assertIn("screenshotPath", output.getvalue())
        self.assertIn("evidenceKind", output.getvalue())

    def test_rejects_non_native_missing_screenshot_and_mislabeled_platform(self):
        records = {}
        for platform in CHECKER.PLATFORMS:
            record = self._base_record()
            record["platform"] = "ios" if platform == "android" else platform
            records[platform] = [record]

        result, output = self._run(records)

        self.assertEqual(1, result, output)
        self.assertIn("declares platform", output)
        self.assertIn("evidenceKind", output)
        self.assertIn("screenshot does not exist", output)

    def test_rejects_different_bounds_key_sets(self):
        records = {}
        for platform in CHECKER.PLATFORMS:
            record = self._base_record()
            record["platform"] = platform
            if platform == "ios":
                record["bounds"] = {"content": [0, 64, 736, 350]}
            records[platform] = [record]

        result, output = self._run(records)

        self.assertEqual(1, result, output)
        self.assertIn("bounds keys differ", output)

    def test_rejects_incomplete_c17_and_c18_native_matrix(self):
        records = {}
        for platform in CHECKER.PLATFORMS:
            c17 = self._base_record("C17-width")
            c17["platform"] = platform
            c17["evidenceKind"] = "native"
            c18 = self._base_record("C18-landscape")
            c18["platform"] = platform
            c18["evidenceKind"] = "native"
            records[platform] = [c17, c18]

        result, output = self._run(records)

        self.assertEqual(1, result, output)
        self.assertIn("width 320", output)
        self.assertIn("C18 required matrix", output)
        self.assertIn("fontScale=1.3", output)
        self.assertIn("keyboardVisible=True", output)

    def test_matrix_requires_every_surface_and_safe_area_variant(self):
        problems = CHECKER.Problems()
        c17 = []
        for surface in CHECKER.REQUIRED_SURFACES:
            for locale in CHECKER.LOCALE_FAMILIES:
                for width in CHECKER.C17_WIDTHS:
                    if surface == "N09" and locale == "en" and width == 375:
                        continue
                    c17.append({
                        "caseId": "C17-width",
                        "surfaceId": surface,
                        "locale": locale,
                        "contentSize": [width, 414],
                        "orientation": "landscape",
                        "safeAreaVariant": "none",
                        "fontScale": 1.0,
                        "keyboardVisible": False,
                    })
        c18 = []
        for surface in CHECKER.REQUIRED_SURFACES:
            for locale in CHECKER.LOCALE_FAMILIES:
                for size in CHECKER.C18_SIZES:
                    for scale in CHECKER.C18_FONT_SCALES:
                        for safe_area in CHECKER.SAFE_AREA_VARIANTS:
                            for keyboard in (False, True):
                                if (surface == "N02" and locale == "zh"
                                        and size == (736, 414) and scale == 1.3
                                        and safe_area == "horizontal-notch" and keyboard):
                                    continue
                                c18.append({
                                    "caseId": "C18-landscape",
                                    "surfaceId": surface,
                                    "locale": locale,
                                    "contentSize": list(size),
                                    "orientation": "landscape",
                                    "safeAreaVariant": safe_area,
                                    "fontScale": scale,
                                    "keyboardVisible": keyboard,
                                })

        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            CHECKER.validate_required_matrices(c17 + c18, problems)

        self.assertEqual(2, problems.missing)
        self.assertIn("surface N09", output.getvalue())
        self.assertIn("safeAreaVariant=horizontal-notch", output.getvalue())


if __name__ == "__main__":
    unittest.main()
