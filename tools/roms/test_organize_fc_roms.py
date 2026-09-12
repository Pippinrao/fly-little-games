import hashlib
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from organize_fc_roms import chinese_title, organize, verify


class OrganizeFcRomsTest(unittest.TestCase):
    def test_every_english_collection_name_has_a_chinese_title(self):
        source = Path(r"D:\FCgames\roms")
        if not source.is_dir():
            self.skipTest("local FC collection is unavailable")
        with self.subTest("all playable packages"):
            for archive in source.glob("*.zip"):
                with zipfile.ZipFile(archive) as opened:
                    playable = any(Path(item.filename).suffix.lower() in {".nes", ".fds", ".unf", ".unif"}
                                   for item in opened.infolist() if not item.is_dir())
                if playable:
                    self.assertRegex(chinese_title(archive.stem), r"[\u3400-\u9fff]", archive.name)

    def test_repack_keeps_payload_and_exposes_chinese_member_name(self):
        payload = b"NES\x1a" + bytes(range(64))
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "source"
            destination = root / "out"
            source.mkdir()
            with zipfile.ZipFile(source / "AKIRA.zip", "w") as archive:
                archive.writestr("AKIRA (J).nes", payload)
            rows, skipped = organize(source, destination, clean=True)
            self.assertEqual([], skipped)
            self.assertEqual("阿基拉", rows[0].title)
            self.assertEqual(hashlib.sha256(payload).hexdigest().upper(), rows[0].sha256)
            with zipfile.ZipFile(destination / rows[0].output) as archive:
                self.assertEqual(["阿基拉.nes"], archive.namelist())
                self.assertEqual(payload, archive.read("阿基拉.nes"))
            self.assertEqual(0, verify(destination))


if __name__ == "__main__":
    unittest.main()
