#!/usr/bin/env python3
"""Black-box tests for deterministic unsupported-payload fixture generation."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SOURCE_GENERATOR = Path(sys.argv.pop(1)).resolve()
TEST_ROOT = Path(sys.argv.pop(1)).resolve()


class UnsupportedPayloadFixtureGeneratorTest(unittest.TestCase):
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

    def test_check_accepts_only_exact_generated_corpus(self) -> None:
        corpus = self.generate()
        self.assertEqual(39, len(list(corpus.iterdir())))
        result = self.run_generator("--check")
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)

        missing = corpus / "plain_ascii.bin"
        missing.unlink()
        result = self.run_generator("--check")
        self.assert_problem(result, "missing", missing.name)
        self.assertFalse(missing.exists())

        self.assertEqual(0, self.run_generator().returncode)
        changed = corpus / "manifest.tsv"
        changed.write_bytes(b"locally changed\n")
        result = self.run_generator("--check")
        self.assert_problem(result, "changed", changed.name)

        self.assertEqual(0, self.run_generator().returncode)
        orphan = corpus / "orphan.bin"
        orphan.write_bytes(b"orphan")
        result = self.run_generator("--check")
        self.assert_problem(result, "unexpected", orphan.name)

    def test_generation_refuses_unexpected_nonfile_and_symlink_entries(self) -> None:
        corpus = self.generate()
        orphan = corpus / "orphan.bin"
        orphan.write_bytes(b"orphan")
        result = self.run_generator()
        self.assert_problem(result, "remove", orphan.name)

        orphan.unlink()
        directory = corpus / "directory.bin"
        directory.mkdir()
        result = self.run_generator()
        self.assert_problem(result, "remove", directory.name)

        directory.rmdir()
        target = self.root / "outside.bin"
        target.write_bytes(b"outside")
        link = corpus / "linked.bin"
        try:
            link.symlink_to(target)
        except OSError as error:
            self.skipTest(f"symlink creation is unavailable: {error}")
        result = self.run_generator()
        self.assert_problem(result, "remove", link.name)
        self.assertEqual(b"outside", target.read_bytes())

    def test_atomic_generation_does_not_mutate_hardlink_alias(self) -> None:
        corpus = self.generate()
        target = corpus / "plain_ascii.bin"
        alias = self.root / "hardlink-alias.bin"
        try:
            os.link(target, alias)
        except OSError as error:
            self.skipTest(f"hard links are unavailable: {error}")
        sentinel = b"outside hardlink must remain untouched"
        alias.write_bytes(sentinel)
        result = self.run_generator()
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertEqual(sentinel, alias.read_bytes())
        self.assertEqual(0, self.run_generator("--check").returncode)

    @unittest.skipUnless(sys.platform == "win32", "Windows junction regression")
    def test_generation_refuses_junction_output_root(self) -> None:
        output = self.root / "v1"
        outside = self.root / "junction-target"
        outside.mkdir()
        created = subprocess.run(
            ["cmd.exe", "/d", "/c", "mklink", "/J", str(output), str(outside)],
            cwd=self.root,
            check=False,
            capture_output=True,
            text=True,
        )
        if created.returncode != 0:
            self.skipTest("junction creation is unavailable")
        try:
            result = self.run_generator()
            self.assert_problem(result, "reparse", output.name)
            self.assertEqual([], list(outside.iterdir()))
        finally:
            if output.exists():
                output.rmdir()


if __name__ == "__main__":
    unittest.main()
