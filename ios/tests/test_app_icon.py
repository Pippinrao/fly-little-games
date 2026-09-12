"""Run with Python + Pillow; no Apple SDK is needed for icon validation."""

import json
from pathlib import Path
import subprocess
import sys
import unittest

from PIL import Image, ImageChops


ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT / "ios/app/Assets.xcassets"
ICONS = CATALOG / "AppIcon.appiconset"


class AppIconTests(unittest.TestCase):
    def test_required_iphone_slots_are_opaque_and_correctly_sized(self):
        manifest = ICONS / "Contents.json"
        self.assertTrue(manifest.is_file(), "iPhone AppIcon catalog is missing")
        contents = json.loads(manifest.read_text(encoding="utf-8"))
        expected = {
            ("iphone", "20x20", "2x"), ("iphone", "20x20", "3x"),
            ("iphone", "29x29", "2x"), ("iphone", "29x29", "3x"),
            ("iphone", "40x40", "2x"), ("iphone", "40x40", "3x"),
            ("iphone", "60x60", "2x"), ("iphone", "60x60", "3x"),
            ("ios-marketing", "1024x1024", "1x"),
        }
        images = contents["images"]
        self.assertEqual(len(images), len(expected))
        self.assertEqual({(i["idiom"], i["size"], i["scale"]) for i in images}, expected)
        self.assertEqual(contents["info"], {"author": "xcode", "version": 1})
        self.assertEqual(json.loads((CATALOG / "Contents.json").read_text())["info"],
                         {"author": "xcode", "version": 1})
        for entry in images:
            with self.subTest(entry=entry), Image.open(ICONS / entry["filename"]) as icon:
                pixels = int(entry["size"].split("x")[0]) * int(entry["scale"][0])
                self.assertEqual(icon.size, (pixels, pixels))
                self.assertEqual(icon.format, "PNG")
                self.assertEqual(icon.mode, "RGB", "App icons must have no alpha channel")
                self.assertNotIn("transparency", icon.info)

    def test_marketing_icon_preserves_selected_master(self):
        path = ICONS / "AppIcon-1024.png"
        self.assertTrue(path.is_file(), "Marketing icon is missing")
        master_path = ROOT / "assets/branding/app-icon-master.png"
        self.assertTrue(master_path.is_file(), "Approved shared icon master is missing")
        with Image.open(path) as icon, Image.open(master_path) as master:
            expected = master.convert("RGB").resize((1024, 1024), Image.Resampling.LANCZOS)
            self.assertIsNone(ImageChops.difference(icon, expected).getbbox())

    def test_committed_assets_match_reproducible_generator(self):
        script = ROOT / "tools/ios/generate_app_icon.py"
        self.assertTrue(script.is_file(), "Reproducible icon generator is missing")
        result = subprocess.run([sys.executable, str(script), "--check"],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
