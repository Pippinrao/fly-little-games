"""Cross-platform resource contracts; run with Python and pinned Pillow."""

import importlib.util
from io import BytesIO
import json
import math
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[2]
ANDROID = "{http://schemas.android.com/apk/res/android}"


class SharedIconTests(unittest.TestCase):
    def test_shared_generator_builds_from_pixels_without_old_vector_dependency(self):
        script = ROOT / "tools/branding/generate_app_icons.py"
        self.assertTrue(script.is_file(), "Shared master generator is missing")
        spec = importlib.util.spec_from_file_location("app_icons", script)
        generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generator)
        # Synthetic test fixture is kept in memory and never becomes product art.
        master = Image.new("RGB", (1024, 1024), (255, 248, 230))
        master.paste((30, 160, 145), (300, 250, 724, 774))
        files = generator.generated_files(master)
        with Image.open(BytesIO(files["ios/app/Assets.xcassets/AppIcon.appiconset/AppIcon-1024.png"])) as ios:
            self.assertIsNone(ImageChops.difference(ios, master).getbbox())
        with Image.open(BytesIO(files["app/src/main/res/drawable-xxxhdpi/ic_launcher_artwork.png"])) as android:
            self.assertEqual(android.size, (432, 432))
            self.assertEqual(android.mode, "RGBA")
            self.assertEqual(android.getchannel("A").getbbox(), (72, 72, 360, 360))
        with Image.open(BytesIO(files["harmony/AppScope/resources/base/media/app_icon.png"])) as harmony:
            self.assertEqual(harmony.size, (288, 288))
            self.assertEqual(harmony.mode, "RGB")

    def test_all_android_entry_points_use_the_shared_art(self):
        resources = ROOT / "app/src/main/res"
        for name in ("ic_launcher", "ic_launcher_round"):
            legacy = ET.parse(resources / f"mipmap-anydpi/{name}.xml").getroot()
            self.assertEqual(legacy.tag, "bitmap", "Legacy icon still uses old vector")
            self.assertEqual(legacy.attrib[ANDROID + "src"], "@mipmap/ic_launcher_legacy")
            for version in (26, 33):
                adaptive = ET.parse(resources / f"mipmap-anydpi-v{version}/{name}.xml").getroot()
                self.assertEqual(adaptive.find("background").attrib[ANDROID + "drawable"],
                                 "@color/ic_launcher_background")
                self.assertEqual(adaptive.find("foreground").attrib[ANDROID + "drawable"],
                                 "@drawable/ic_launcher_foreground")
                if version == 33:
                    self.assertEqual(adaptive.find("monochrome").attrib[ANDROID + "drawable"],
                                     "@drawable/ic_launcher_monochrome")
        for name, source in [("foreground", "artwork"), ("monochrome", "themed")]:
            root = ET.parse(resources / f"drawable/ic_launcher_{name}.xml").getroot()
            self.assertEqual(root.tag, "bitmap")
            self.assertEqual(root.attrib[ANDROID + "src"], f"@drawable/ic_launcher_{source}")

    def test_harmony_same_named_resource_has_no_duplicate_svg(self):
        media = ROOT / "harmony/AppScope/resources/base/media"
        self.assertTrue((media / "app_icon.png").is_file(), "Harmony PNG is missing")
        self.assertFalse((media / "app_icon.svg").exists(), "Duplicate app_icon resource")
        for path in (ROOT / "harmony/AppScope/app.json5", ROOT / "harmony/entry/src/main/module.json5"):
            self.assertIn('"icon": "$media:app_icon"', path.read_text())

    def test_adaptive_subject_is_inside_android_safe_circle(self):
        path = ROOT / "app/src/main/res/drawable-xxxhdpi/ic_launcher_themed.png"
        self.assertTrue(path.is_file(), "Themed cartridge silhouette is missing")
        with Image.open(path) as icon:
            alpha = icon.getchannel("A")
            points = [(x, y) for y in range(432) for x in range(432)
                      if alpha.getpixel((x, y)) > 127]
            self.assertTrue(points)
            # The 66dp safe circle has radius 132px on a 432px/108dp layer.
            self.assertLessEqual(max(math.hypot(x-216, y-216) for x, y in points), 132)

    def test_master_has_plain_cream_edges_and_no_preapplied_mask(self):
        path = ROOT / "assets/branding/app-icon-master.png"
        self.assertTrue(path.is_file(), "Approved shared master is missing")
        with Image.open(path) as master:
            self.assertEqual(master.mode, "RGB")
            self.assertEqual(master.width, master.height)
            self.assertGreaterEqual(master.width, 1024)
            for x, y in [(0, 0), (master.width-1, 0), (0, master.height-1),
                         (master.width-1, master.height-1), (master.width//2, 0),
                         (0, master.height//2)]:
                self.assertLessEqual(max(abs(a-b) for a, b in zip(master.getpixel((x, y)),
                                                               (255, 248, 230))), 8)


if __name__ == "__main__":
    unittest.main()
