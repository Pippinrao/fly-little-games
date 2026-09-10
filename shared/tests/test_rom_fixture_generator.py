#!/usr/bin/env python3
"""Black-box tests for deterministic ROM fixture generation and checking."""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SOURCE_GENERATOR = Path(sys.argv.pop(1)).resolve()
TEST_ROOT = Path(sys.argv.pop(1)).resolve()


class RomFixtureGeneratorTest(unittest.TestCase):
    def setUp(self) -> None:
        TEST_ROOT.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=TEST_ROOT)
        self.root = Path(self.temporary.name)
        self.generator = self.root / "generate_v1.py"
        shutil.copyfile(SOURCE_GENERATOR, self.generator)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def run_generator(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(self.generator), *arguments],
            cwd=self.root,
            check=False,
            capture_output=True,
            text=True,
        )

    def generate(self) -> Path:
        result = self.run_generator()
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        return self.root / "v1"

    def assert_problem(
        self,
        result: subprocess.CompletedProcess[str],
        problem: str,
        filename: str,
    ) -> None:
        output = (result.stdout + result.stderr).lower()
        self.assertNotEqual(0, result.returncode, output)
        self.assertIn(problem, output)
        self.assertIn(filename.lower(), output)

    def test_check_accepts_exact_generated_corpus(self) -> None:
        self.generate()

        result = self.run_generator("--check")

        self.assertEqual(0, result.returncode, result.stdout + result.stderr)

    def test_check_reports_missing_file_without_recreating_it(self) -> None:
        corpus = self.generate()
        missing = corpus / "unknown_bytes.bin"
        missing.unlink()

        result = self.run_generator("--check")

        self.assert_problem(result, "missing", missing.name)
        self.assertFalse(missing.exists(), "--check must not recreate a missing fixture")

    def test_check_reports_changed_file_without_overwriting_it(self) -> None:
        corpus = self.generate()
        changed = corpus / "unknown_bytes.bin"
        replacement = b"locally changed"
        changed.write_bytes(replacement)

        result = self.run_generator("--check")

        self.assert_problem(result, "changed", changed.name)
        self.assertEqual(replacement, changed.read_bytes(), "--check must not rewrite fixtures")

    def test_check_reports_unexpected_orphan_without_deleting_it(self) -> None:
        corpus = self.generate()
        orphan = corpus / "orphan.bin"
        orphan.write_bytes(b"orphan")

        result = self.run_generator("--check")

        self.assert_problem(result, "unexpected", orphan.name)
        self.assertTrue(orphan.exists(), "--check must not delete unexpected files")

    def test_default_generation_refuses_to_leave_an_orphan(self) -> None:
        corpus = self.generate()
        orphan = corpus / "orphan.bin"
        orphan.write_bytes(b"orphan")

        result = self.run_generator()

        self.assert_problem(result, "remove", orphan.name)
        self.assertTrue(orphan.exists(), "generation must not silently delete an orphan")


if __name__ == "__main__":
    unittest.main()
