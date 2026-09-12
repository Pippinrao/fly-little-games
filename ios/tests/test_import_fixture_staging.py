"""Host-only tests: fake simulator metadata; no simctl, device, or private ROM access."""
import importlib.util
import json
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest import mock

REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("stage_import_fixtures", REPO / "ios/scripts/stage_import_fixtures.py")
STAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(STAGE)
UDID = "F6C44A31-0A5F-40B9-BB7C-E194D80540C3"
IDENTIFIER = "group.com.apple.FileProvider.LocalStorage"
METADATA = ".com.apple.mobile_container_manager.metadata.plist"


class ImportFixtureStagingTests(unittest.TestCase):
    def setUp(self):
        evidence = REPO / "build" / "import-staging-host-tests"
        evidence.mkdir(parents=True, exist_ok=True)
        self.scratch = tempfile.TemporaryDirectory(dir=evidence)
        self.addCleanup(self.scratch.cleanup)
        self.home = Path(self.scratch.name).resolve()
        self.data = self.home / "Library/Developer/CoreSimulator/Devices" / UDID / "data"
        self.groups = self.data / "Containers/Shared/AppGroup"
        self.groups.mkdir(parents=True)

    def add_group(self, name, identifier=IDENTIFIER, storage=True):
        group = self.groups / name
        group.mkdir()
        (group / METADATA).write_bytes(plistlib.dumps({"MCMMetadataIdentifier": identifier}, fmt=plistlib.FMT_BINARY))
        provider = group / "File Provider Storage"
        if storage:
            provider.mkdir()
        return provider

    def run_stage(self, *extra):
        devices = {"devices": {"iOS-16-4": [{"udid": UDID, "state": "Booted"}]}}
        with mock.patch.object(STAGE.sys, "argv", ["stage_import_fixtures.py", "--udid", UDID, *extra]), \
             mock.patch.object(STAGE.sys, "platform", "darwin"), \
             mock.patch.object(STAGE.Path, "home", return_value=self.home), \
             mock.patch.object(STAGE.subprocess, "check_output", return_value=json.dumps(devices)):
            try:
                STAGE.main()
            except SystemExit as error:
                self.fail(f"valid explicit UDID should discover a provider without --provider-root: {error}")

    def test_unique_metadata_provider_is_discovered_and_documents_is_untouched(self):
        documents = self.data / "Documents"
        documents.mkdir()
        expected = self.add_group("actual-local-storage")
        unrelated = self.add_group("unrelated", IDENTIFIER + ".other")
        self.run_stage()
        STAGE.validate_existing(expected / STAGE.FOLDER, STAGE.fixture_files())
        self.assertEqual(list(documents.iterdir()), [])
        self.assertEqual(list(unrelated.iterdir()), [])

    def test_duplicate_metadata_identity_is_refused(self):
        self.add_group("one")
        self.add_group("two")
        with self.assertRaisesRegex(ValueError, "exactly one"):
            self.run_stage()

    def test_similar_identifier_is_not_accepted(self):
        self.add_group("not-local", IDENTIFIER + ".other")
        with self.assertRaisesRegex(ValueError, "exactly one"):
            self.run_stage()

    def test_storage_must_already_exist(self):
        missing = self.add_group("not-initialized", storage=False)
        with self.assertRaisesRegex(ValueError, "File Provider Storage"):
            self.run_stage()
        self.assertFalse(missing.exists())

    def test_explicit_override_keeps_outside_device_boundary_check(self):
        with self.assertRaisesRegex(ValueError, "strictly inside"):
            self.run_stage("--provider-root", str(self.home))

    def test_symlink_metadata_is_refused_before_reading(self):
        provider = self.add_group("local")
        metadata = provider.parent / METADATA
        original = Path.is_symlink
        with mock.patch.object(Path, "is_symlink", lambda path: path == metadata or original(path)):
            with self.assertRaisesRegex(ValueError, "symlink"):
                self.run_stage()


if __name__ == "__main__":
    unittest.main()
