#!/usr/bin/env python3
"""Runs the real install/uninstall scripts in a temporary home and fake system directory."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import signal
import time

import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "support"))
from harness import Sandbox, start_process, stop, test_main, script, snapshot

source = Path(__file__).resolve().parents[2]
build = Path(sys.argv[1]).resolve()


class InstallationTest(unittest.TestCase):
    def setUp(self):
        self.sandbox = self.enterContext(Sandbox(build, self._testMethodName))
        self.temporary = self.sandbox.root
        self.home = self.sandbox.home
        self.system = self.sandbox.system
        self.env = self.sandbox.env | {
            "HOME": str(self.home),
            "XDG_CONFIG_HOME": str(self.home / ".config"),
            "XDG_DATA_HOME": str(self.home / ".local/share"),
            "XDG_STATE_HOME": str(self.home / ".local/state"),
            "XDG_CACHE_HOME": str(self.home / ".cache"),
            "DBUS_SESSION_BUS_ADDRESS": "unix:path=/nonexistent-pointeraccel-test",
            "POINTERACCEL_TEST_NO_BUILD": "1",
        }
        self.kwin = self.home / ".config/kwinrc"
        self.kwin.parent.mkdir(parents=True)
        self.original = b"[Windows]\nFocusPolicy=ClickToFocus\n\n[Plugins]\notherPluginEnabled=true\n"
        self.kwin.write_bytes(self.original)
        self.install_command = ["bash", source / "install.sh", "--test-system", self.system, "--build-dir", build]
        self.uninstall_command = ["bash", source / "uninstall.sh", "--offline", "--test-system", self.system]
        self.before = snapshot(self.temporary)

    def execute(self, arguments, expected=0, extra=None):
        return self.sandbox.run(arguments, env=self.env | (extra or {}), expected=expected, timeout=15)

    def test_recovery_upgrade_and_legacy_migration(self):
        # A partial package update must fail preflight before creating any install files.
        with tempfile.TemporaryDirectory(prefix="pointeraccel-rpm-") as fake_bin:
            fake_rpm = Path(fake_bin) / "rpm"
            fake_rpm.write_text(
                script(f"""#!/usr/bin/env python3
    import subprocess, sys
    result = subprocess.check_output([{shutil.which("rpm")!r}] + sys.argv[1:]).decode()
    if sys.argv[-1] == "kwin-devel":
        result = result.replace("-", "-pointeraccel-test.", 1)
    print(result, end="")
    """)
            )
            fake_rpm.chmod(0o755)
            self.execute(self.install_command, 1, {"PATH": fake_bin + ":" + self.env["PATH"]})
            self.assertEqual(snapshot(Path(self.temporary)), self.before, "mismatched packages mutated installation")

        def interrupt_and_recover(expected_snapshot):
            paused = Path(self.temporary) / "paused"
            process = start_process(
                [str(a) for a in self.install_command],
                env=self.env | {"POINTERACCEL_TEST_PAUSE_AFTER_MODULE": str(paused)},
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            try:
                deadline = time.monotonic() + 10
                while not paused.exists() and time.monotonic() < deadline:
                    self.assertTrue(process.poll() is None, "installer exited before interruption point")
                    time.sleep(0.05)
                self.assertTrue(paused.exists(), "installer did not reach interruption point")
            finally:
                os.killpg(process.pid, signal.SIGKILL)
                process.communicate(timeout=5)
                stop(process)
            paused.unlink()
            self.assertTrue((self.system / "records/pending").exists())
            self.execute(self.install_command, 1)
            self.execute(self.install_command + ["--recover"])
            self.assertEqual(
                snapshot(Path(self.temporary)), expected_snapshot, "interrupted install recovery lost original files"
            )

        interrupt_and_recover(self.before)
        paused = Path(self.temporary) / "committed-pause"
        process = start_process(
            [str(a) for a in self.install_command],
            env=self.env | {"POINTERACCEL_TEST_PAUSE_AFTER_COMMIT": str(paused)},
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            deadline = time.monotonic() + 10
            while not paused.exists() and time.monotonic() < deadline:
                self.assertTrue(process.poll() is None)
                time.sleep(0.05)
            self.assertTrue(paused.exists())
        finally:
            os.killpg(process.pid, signal.SIGKILL)
            process.communicate(timeout=5)
            stop(process)
        paused.unlink()
        self.assertFalse((self.system / "records/pending").exists())
        self.execute(self.install_command + ["--recover"])
        self.assertTrue((self.system / "plugins/pointeraccel_kwin.so").exists(), "recovery removed a committed module")
        self.execute(self.uninstall_command)
        self.assertEqual(snapshot(Path(self.temporary)), self.before)
        self.execute(self.install_command, 1, {"POINTERACCEL_TEST_FAIL_AFTER_MODULE": "1"})
        self.assertEqual(snapshot(Path(self.temporary)), self.before, "failed first install did not roll back")
        self.execute(self.install_command)
        data = self.home / ".local/share/pointeraccel"
        notice = (data / "licenses/windows-pointer-linux.txt").read_bytes()
        # Preserve the original upstream notice byte-for-byte after moving it into README.
        self.assertEqual(
            hashlib.sha256(notice).hexdigest(), "6e32a6f6623073fc97f5d043c253e9ef566ac534ff438a2780f56abc0c740990"
        )
        for name in ("README.md", "README.zh-CN.md"):
            document = (data / name).read_bytes()
            self.assertEqual(document, (source / name).read_bytes())
            self.assertTrue(notice in document)
        cli = self.home / ".local/bin/pointeraccel"
        self.execute([cli, "config", "set", "pointer-speed", "14/20"])
        self.execute([cli, "config", "set", "enhance-pointer-precision", "off"])
        self.assertEqual(json.loads(self.execute([cli, "config", "get", "--json"]))["pointer_speed"], 14)
        self.execute([cli, "config", "set", "pointer-speed", "21/20"], 2)
        gui = self.home / ".local/share/pointeraccel/bin/pointeraccel-gui"
        self.assertTrue(gui.is_file())
        saved_config = (self.home / ".config/pointeraccel/config.json").read_bytes()
        # Runtime font caches are not installation-owned files.
        with tempfile.TemporaryDirectory(prefix="pointeraccel-gui-cache-") as gui_cache:
            process = start_process(
                [cli, "gui"],
                env=self.env | {"QT_QPA_PLATFORM": "offscreen", "XDG_CACHE_HOME": gui_cache},
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            try:
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline and process.poll() is None:
                    if Path(f"/proc/{process.pid}/exe").resolve() == gui:
                        break
                    time.sleep(0.02)
                self.assertTrue(process.poll() is None, "installed GUI exited during startup")
                self.assertEqual(
                    Path(f"/proc/{process.pid}/exe").resolve(), gui, "CLI did not replace itself with the GUI"
                )
                time.sleep(0.2)
                self.assertTrue(process.poll() is None, "installed QWidget GUI failed to start")
            finally:
                stop(process)
                process.communicate(timeout=5)
        self.assertEqual((self.home / ".config/pointeraccel/config.json").read_bytes(), saved_config)
        installed = snapshot(Path(self.temporary))
        interrupt_and_recover(installed)
        self.execute(self.install_command, 1, {"POINTERACCEL_TEST_FAIL_AFTER_MODULE": "1"})
        self.assertEqual(
            snapshot(Path(self.temporary)), installed, "failed upgrade did not restore previous installation"
        )
        self.execute(self.install_command, 1, {"POINTERACCEL_TEST_FAIL_BEFORE_COMMIT": "1"})
        self.assertEqual(
            snapshot(Path(self.temporary)),
            installed,
            "late upgrade failure did not restore both manifests and services",
        )
        self.execute(self.install_command)
        self.assertEqual(json.loads(self.execute([cli, "config", "get", "--json"]))["pointer_speed"], 14)
        # Recreate the legacy layout (direct KWin autostart, two-column ownership,
        # no compatibility record/service/snapshot) and migrate without replacing
        # the original backup or the user's settings.
        state = self.home / ".local/state/pointeraccel"
        data = self.home / ".local/share/pointeraccel"
        backup = (state / "backups/kwin.json").read_bytes()
        removed = [
            data / "bin/pointeraccel-gui",
            data / "source.tar.gz",
            data / "scripts/maintenance.py",
            state / "session.json",
            self.home / ".config/systemd/user/pointeraccel-session.service",
            self.home / ".config/systemd/user/plasma-workspace.target.wants/pointeraccel-session.service",
        ]
        manifest = state / "manifest.tsv"
        manifest.write_text(
            "".join(
                line + "\n"
                for line in manifest.read_text().splitlines()
                if line.split("\t")[-1] not in {str(path) for path in removed}
            )
        )
        for path in removed:
            path.unlink()
        (self.system / "records/compatibility.json").unlink()
        owner = self.system / "records/owner.tsv"
        owner.write_text("\t".join(owner.read_text().strip().split("\t")[:2]) + "\n")
        digest = self.execute([cli, "internal-plugin", "enable"]).strip()
        (state / "autostart.sha256").write_text(digest + "\n")
        self.execute(self.install_command)
        self.assertTrue(gui.is_file(), "upgrade from a CLI-only installation did not add the GUI")
        self.assertEqual((state / "backups/kwin.json").read_bytes(), backup)
        self.assertTrue(b"pointeraccel_kwinEnabled=false" in self.kwin.read_bytes())
        self.assertEqual(json.loads(self.execute([cli, "config", "get", "--json"]))["pointer_speed"], 14)
        self.execute(self.uninstall_command)
        self.assertEqual(self.kwin.read_bytes(), self.original, "kwinrc was not restored byte-for-byte")
        self.assertEqual(
            snapshot(Path(self.temporary)),
            self.before,
            (
                "uninstall left owned files",
                set(snapshot(Path(self.temporary))) - set(self.before),
            ),
        )
        self.execute(self.uninstall_command)

    def test_preserves_preexisting_configuration(self):
        # Preserve new, unrelated KWin changes and a configuration that predates installation.
        config = self.home / ".config/pointeraccel/config.json"
        config.parent.mkdir()
        initial = {
            "enabled": False,
            "pointer_speed": 7,
            "enhance_pointer_precision": True,
            "excluded_devices": [],
            "trace": False,
        }
        config.write_text(json.dumps(initial))
        original_config = config.read_bytes()
        self.execute(self.install_command)
        self.kwin.write_bytes(self.kwin.read_bytes() + b"\n[UserChange]\nKeepThis=true\n")
        self.execute(self.uninstall_command)
        self.assertTrue(
            b"KeepThis=true" in self.kwin.read_bytes() and b"pointeraccel_kwinEnabled" not in self.kwin.read_bytes()
        )
        self.assertEqual(config.read_bytes(), original_config)

    def test_modified_file_blocks_uninstall(self):
        # A changed installed file must be detected before any uninstall mutation.
        self.execute(self.install_command)
        document = self.home / ".local/share/pointeraccel/README.md"
        document.write_text("changed by user")
        conflicted = snapshot(Path(self.temporary))
        self.execute(self.uninstall_command, 1)
        self.assertEqual(snapshot(Path(self.temporary)), conflicted)


if __name__ == "__main__":
    test_main(build)
