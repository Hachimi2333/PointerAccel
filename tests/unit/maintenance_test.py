#!/usr/bin/env python3
"""Check package compatibility policy while retaining real ELF fingerprints."""

import importlib.util
from pathlib import Path
import subprocess
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location(
    "maintenance", Path(__file__).resolve().parents[2] / "scripts/maintenance.py"
)
maintenance = importlib.util.module_from_spec(spec)
spec.loader.exec_module(maintenance)


class CompatibilityPreflight(unittest.TestCase):
    def packages(self, version, overrides=None):
        builds = dict.fromkeys(("kwin", "kwin-libs", "kwin-devel"), f"0:{version}-2.fc44.x86_64")
        builds.update(overrides or {})

        def query(arguments, **kwargs):
            build = builds[arguments[-1]]
            package_version = build.split(":", 1)[1].split("-", 1)[0] if arguments[-1] in (overrides or {}) else version
            return subprocess.CompletedProcess(arguments, 0, package_version + "\t" + build)

        return mock.patch.object(maintenance.subprocess, "run", side_effect=query)

    def test_supported_series_retain_file_and_package_identities(self):
        for version in ("6.7.0", "6.7.5", "6.8.0", "6.8.2"):
            with self.subTest(version=version), self.packages(version):
                record = maintenance.fingerprint()
                self.assertEqual(record["kwin_version"], version)
                self.assertEqual(record["kwin_library"], str(maintenance.LIBRARY.resolve(strict=True)))
                self.assertEqual(record["kwin_sha256"], maintenance.digest(maintenance.LIBRARY))
                self.assertEqual(record["executable_build_id"], maintenance.elf_id(maintenance.EXECUTABLE))
                self.assertEqual(len(set(record["packages"].values())), 1)

    def test_unreviewed_series_are_rejected(self):
        for version in ("6.6.9", "6.9.0", "7.0.0", "6.8.2-rc1"):
            with self.subTest(version=version), self.packages(version), self.assertRaisesRegex(ValueError, "尚未适配"):
                maintenance.fingerprint()

    def test_mismatched_runtime_library_or_development_build_is_rejected(self):
        for package in ("kwin", "kwin-libs", "kwin-devel"):
            for build in (
                "0:6.7.5-2.fc44.x86_64",
                "0:6.8.2-3.fc44.x86_64",
                "1:6.8.2-2.fc44.x86_64",
                "0:6.8.2-2.fc44.aarch64",
            ):
                with (
                    self.subTest(package=package, build=build),
                    self.packages("6.8.2", {package: build}),
                    self.assertRaisesRegex(ValueError, "版本/发行号不匹配"),
                ):
                    maintenance.fingerprint()

    def test_same_version_rebuild_during_build_is_rejected(self):
        with self.packages("6.8.2"):
            record = maintenance.fingerprint()
            maintenance.verify(record)
        updated = dict.fromkeys(record["packages"], "0:6.8.2-3.fc44.x86_64")
        with self.packages("6.8.2", updated), self.assertRaisesRegex(ValueError, "构建期间 KWin 已变化"):
            maintenance.verify(record)


if __name__ == "__main__":
    unittest.main()
