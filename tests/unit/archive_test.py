"""Deterministic source archives and data-only extraction boundaries."""

import io
from pathlib import Path
import sys
import tarfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "support"))
from harness import SOURCE, Sandbox, test_main

BUILD = Path(sys.argv[1]).resolve()


class SourceArchive(unittest.TestCase):
    def test_archive_contains_complete_source_and_is_reproducible(self):
        with Sandbox(BUILD, self._testMethodName) as sandbox:
            first, second = sandbox.root / "first.tar.gz", sandbox.root / "second.tar.gz"
            for archive in (first, second):
                sandbox.run([sys.executable, SOURCE / "scripts/maintenance.py", "snapshot", SOURCE, archive])
            self.assertEqual(first.read_bytes(), second.read_bytes())
            unpacked = sandbox.root / "unpacked"
            unpacked.mkdir()
            sandbox.run([sys.executable, SOURCE / "scripts/maintenance.py", "extract", first, unpacked])
            for item in (
                "AGENTS.md",
                "CMakeLists.txt",
                "CMakePresets.json",
                ".clang-format",
                ".editorconfig",
                ".cmake-format.json",
                "cmake/VerifyFrozen.cmake",
                "tests/CMakeLists.txt",
                "tests/support/session-bus.conf",
                "tests/fixtures/input_equivalence.json",
                "src/gui/draft.cpp",
                "src/kwin/input_session.cpp",
            ):
                with self.subTest(item=item):
                    self.assertEqual((unpacked / item).read_bytes(), (SOURCE / item).read_bytes())
            self.assertFalse(list(unpacked.rglob("__pycache__")))

    def test_unsafe_members_are_rejected_before_any_extraction(self):
        for name, kind in [
            ("../outside", tarfile.REGTYPE),
            ("/absolute", tarfile.REGTYPE),
            ("linked", tarfile.SYMTYPE),
            ("special", tarfile.FIFOTYPE),
        ]:
            with self.subTest(name=name), Sandbox(BUILD, self._testMethodName) as sandbox:
                archive = sandbox.root / "unsafe.tar"
                with tarfile.open(archive, "w") as stream:
                    safe = tarfile.TarInfo("safe")
                    safe.size = 2
                    stream.addfile(safe, io.BytesIO(b"ok"))
                    unsafe = tarfile.TarInfo(name)
                    unsafe.type = kind
                    unsafe.linkname = "/outside"
                    stream.addfile(unsafe)
                destination = sandbox.root / "destination"
                destination.mkdir()
                sandbox.run(
                    [sys.executable, SOURCE / "scripts/maintenance.py", "extract", archive, destination], expected=1
                )
                self.assertEqual(list(destination.iterdir()), [])


if __name__ == "__main__":
    test_main(BUILD)
