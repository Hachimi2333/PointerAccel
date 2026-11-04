"""Check actual linked artifacts, including absence of test hooks in production."""

import json
from pathlib import Path
import subprocess
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "support"))
from harness import start_process, test_main

BUILD = Path(sys.argv[1]).resolve()


class ArtifactBoundaries(unittest.TestCase):
    def output(self, *command):
        return subprocess.check_output([str(a) for a in command], text=True, timeout=10)

    def test_linked_dependencies(self):
        for program, forbidden in [
            ("pa_engine_test", ("libQt6", "libkwin")),
            ("pointeraccel", ("libQt6Gui", "libQt6Widgets", "libQt6Quick", "libQt6Qml", "libkwin")),
            ("pointeraccel-gui", ("libkwin", "libQt6Quick", "libQt6Qml")),
        ]:
            with self.subTest(program=program):
                dependencies = self.output("ldd", BUILD / program)
                for library in forbidden:
                    self.assertNotIn(library, dependencies)

    def test_nested_compositor_refuses_an_unisolated_bus(self):
        for environment in (
            {},
            {"DBUS_SESSION_BUS_ADDRESS": "desktop"},
            {
                "POINTERACCEL_PRIVATE_BUS": "1",
                "DBUS_SESSION_BUS_ADDRESS": "desktop",
                "POINTERACCEL_PARENT_BUS": "desktop",
            },
        ):
            with self.subTest(environment=environment), mock.patch("harness.subprocess.Popen") as spawn:
                with self.assertRaises(RuntimeError):
                    start_process(["/usr/bin/kwin_wayland", "--virtual"], env=environment)
                spawn.assert_not_called()

    def test_every_ctest_case_has_labels_and_timeout(self):
        listing = json.loads(self.output("ctest", "--test-dir", BUILD, "--show-only=json-v1"))
        self.assertTrue(listing["tests"])
        for test in listing["tests"]:
            with self.subTest(test=test["name"]):
                properties = {p["name"]: p["value"] for p in test["properties"]}
                self.assertTrue(properties.get("LABELS"))
                self.assertGreater(properties.get("TIMEOUT", 0), 0)

    def test_injection_symbols_only_exist_in_test_module(self):
        production = self.output(
            "nm", "--defined-only", "-C", BUILD / "stage/qt6/plugins/kwin/plugins/pointeraccel_kwin.so"
        )
        testing = self.output(
            "nm", "--defined-only", "-C", BUILD / "teststage/qt6/plugins/kwin/plugins/pointeraccel_kwin.so"
        )
        for symbol in ("TestReports(", "TestHotplug(", "TestEnvironment(", "appendForTest(", "TestBackend"):
            with self.subTest(symbol=symbol):
                self.assertNotIn(symbol, production)
                self.assertIn(symbol, testing)


if __name__ == "__main__":
    test_main(BUILD)
