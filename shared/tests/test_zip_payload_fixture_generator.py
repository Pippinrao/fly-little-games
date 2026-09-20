#!/usr/bin/env python3
"""Black-box tests for deterministic ZIP-payload fixture generation and checking."""

from __future__ import annotations

import os
import runpy
import shutil
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest import mock


SOURCE_GENERATOR = Path(sys.argv.pop(1)).resolve()
TEST_ROOT = Path(sys.argv.pop(1)).resolve()


class ZipPayloadFixtureGeneratorTest(unittest.TestCase):
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

    def test_frozen_corpus_survives_alternate_compression_for_pinned_payloads(self) -> None:
        generator = runpy.run_path(str(self.generator))
        original = generator["raw_deflate"]
        pinned_payloads = {
            bytes((index * 73 + 19) & 0xFF for index in range(9001)),
            b"directory entries are selected like files",
            b"truncated raw deflate",
        }

        def alternate_deflate(payload: bytes) -> bytes:
            if payload not in pinned_payloads:
                return original(payload)
            compressor = zlib.compressobj(level=0, wbits=-15)
            return compressor.compress(payload) + compressor.flush()

        with mock.patch.dict(generator["fixtures"].__globals__, raw_deflate=alternate_deflate):
            corpus = generator["expected_corpus"]()
        frozen = SOURCE_GENERATOR.parent / "v1"
        self.assertEqual({path.name for path in frozen.iterdir()}, set(corpus))
        for name, contents in corpus.items():
            with self.subTest(name=name):
                self.assertEqual((frozen / name).read_bytes(), contents)

    def test_pinned_success_streams_decode_to_declared_payloads(self) -> None:
        generator = runpy.run_path(str(self.generator))
        original = generator["build_zip"]
        entries = {}

        def capture_entries(specs):
            entries.update((entry.raw_name, entry) for entry in specs)
            return original(specs)

        with mock.patch.dict(generator["fixtures"].__globals__, build_zip=capture_entries):
            generator["fixtures"]()
        for name in (b"large-deflate.nes", b"folder/"):
            with self.subTest(name=name):
                entry = entries[name]
                self.assertIsNotNone(entry.compressed_payload)
                decoder = zlib.decompressobj(wbits=-15)
                self.assertEqual(entry.payload, decoder.decompress(entry.compressed_payload))
                self.assertTrue(decoder.eof)
                self.assertEqual(b"", decoder.unused_data)

    def test_pinned_truncated_stream_is_full_stream_without_final_byte(self) -> None:
        generator = runpy.run_path(str(self.generator))
        full_stream = generator.get("TRUNCATED_RAW_DEFLATE")
        self.assertIsNotNone(full_stream)
        self.assertEqual(b"truncated raw deflate", zlib.decompress(full_stream, wbits=-15))
        original = generator["build_zip"]
        entries = {}

        def capture_entries(specs):
            entries.update((entry.raw_name, entry) for entry in specs)
            return original(specs)

        with mock.patch.dict(generator["fixtures"].__globals__, build_zip=capture_entries):
            fixtures = generator["fixtures"]()
        self.assertEqual(full_stream[:-1], entries[b"truncated.nes"].compressed_payload)
        decoder = zlib.decompressobj(wbits=-15)
        decoder.decompress(entries[b"truncated.nes"].compressed_payload)
        self.assertFalse(decoder.eof)
        fixture = next(item for item in fixtures if item.case_id == "truncated_deflate_incomplete")
        self.assertEqual("INVALID_ZIP", fixture.error_code)
        self.assertEqual("ZIP deflate stream ended before completion", fixture.error_message)

    def test_check_is_read_only_for_missing_changed_and_unexpected_files(self) -> None:
        corpus = self.generate()
        missing = corpus / "stored_exact_limit_local_extra.zip"
        missing.unlink()
        result = self.run_generator("--check")
        self.assert_problem(result, "missing", missing.name)
        self.assertFalse(missing.exists(), "--check must not recreate missing fixtures")

        self.run_generator()
        changed = corpus / "stored_exact_limit_local_extra.zip"
        replacement = b"locally changed"
        changed.write_bytes(replacement)
        result = self.run_generator("--check")
        self.assert_problem(result, "changed", changed.name)
        self.assertEqual(replacement, changed.read_bytes(), "--check must not rewrite fixtures")

        self.run_generator()
        orphan = corpus / "orphan.zip"
        orphan.write_bytes(b"orphan")
        result = self.run_generator("--check")
        self.assert_problem(result, "unexpected", orphan.name)
        self.assertTrue(orphan.exists(), "--check must not delete unexpected files")

    def test_generation_refuses_unexpected_and_non_file_entries(self) -> None:
        corpus = self.generate()
        orphan = corpus / "orphan.zip"
        orphan.write_bytes(b"orphan")
        result = self.run_generator()
        self.assert_problem(result, "remove", orphan.name)
        self.assertTrue(orphan.exists())

        orphan.unlink()
        directory = corpus / "directory.zip"
        directory.mkdir()
        result = self.run_generator()
        self.assert_problem(result, "remove", directory.name)
        self.assertTrue(directory.is_dir())

    def test_generation_refuses_a_symlink_entry_when_supported(self) -> None:
        corpus = self.generate()
        link = corpus / "linked.zip"
        target = self.root / "outside.zip"
        target.write_bytes(b"outside")
        try:
            link.symlink_to(target)
        except OSError as error:
            self.skipTest(f"symlink creation is unavailable: {error}")
        result = self.run_generator()
        self.assert_problem(result, "remove", link.name)
        self.assertEqual(b"outside", target.read_bytes())

    def test_generation_replaces_hardlink_without_mutating_alias(self) -> None:
        corpus = self.generate()
        target = corpus / "stored_exact_limit_local_extra.zip"
        alias = self.root / "hardlink-alias.zip"
        try:
            os.link(target, alias)
        except OSError as error:
            self.skipTest(f"hard links are unavailable: {error}")
        sentinel = b"outside hardlink must remain untouched"
        alias.write_bytes(sentinel)
        result = self.run_generator()
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertEqual(sentinel, alias.read_bytes())
        check = self.run_generator("--check")
        self.assertEqual(0, check.returncode, check.stdout + check.stderr)

    @unittest.skipUnless(sys.platform == "win32", "Windows junction regression")
    def test_generation_refuses_a_junction_output_root(self) -> None:
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
            self.skipTest("junction creation is unavailable: " + created.stdout + created.stderr)
        try:
            result = self.run_generator()
            self.assert_problem(result, "reparse", output.name)
            self.assertEqual([], list(outside.iterdir()))
        finally:
            if output.exists():
                output.rmdir()


if __name__ == "__main__":
    unittest.main()
