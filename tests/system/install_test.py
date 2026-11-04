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
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'support'))
from harness import Sandbox, dbus_call, start_process, stop, test_main, wait_for, script, snapshot, digest

source = Path(__file__).resolve().parents[2]
build = Path(sys.argv[1]).resolve()



class InstallationTest(unittest.TestCase):
    def test_recovery_upgrade_and_legacy_migration(self):
        with Sandbox(build, self._testMethodName) as sandbox:
            temporary = sandbox.root
            home = Path(temporary) / "home"
            system = Path(temporary) / "system"
            env = sandbox.env | {
                "HOME": str(home),
                "XDG_CONFIG_HOME": str(home / ".config"),
                "XDG_DATA_HOME": str(home / ".local/share"),
                "XDG_STATE_HOME": str(home / ".local/state"),
                "XDG_CACHE_HOME": str(home / ".cache"),
                "DBUS_SESSION_BUS_ADDRESS": "unix:path=/nonexistent-pointeraccel-test",
                "POINTERACCEL_TEST_NO_BUILD": "1",
                "POINTERACCEL_ISOLATED_TEST": "1",
                "POINTERACCEL_TEST_SYSTEM_ROOT": str(system),
            }
            kwin = home / ".config/kwinrc"
            kwin.parent.mkdir(parents=True)
            original = b"[Windows]\nFocusPolicy=ClickToFocus\n\n[Plugins]\notherPluginEnabled=true\n"
            kwin.write_bytes(original)

            def run(arguments, expected=0, extra=None):
                return sandbox.run(arguments, env=env | (extra or {}), expected=expected, timeout=15)

            install = ["bash", source / "install.sh", "--test-system", system, "--build-dir", build]
            uninstall = ["bash", source / "uninstall.sh", "--offline", "--test-system", system]
            before = snapshot(Path(temporary))

            # A partial package update must fail preflight before creating any install files.
            with tempfile.TemporaryDirectory(prefix="pointeraccel-rpm-") as fake_bin:
                fake_rpm = Path(fake_bin) / "rpm"
                fake_rpm.write_text(script(f'''#!/usr/bin/env python3
        import subprocess, sys
        result = subprocess.check_output([{shutil.which("rpm")!r}] + sys.argv[1:]).decode()
        if sys.argv[-1] == "kwin-devel":
            result = result.replace("-", "-pointeraccel-test.", 1)
        print(result, end="")
        '''))
                fake_rpm.chmod(0o755)
                run(install, 1, {"PATH": fake_bin + ":" + env["PATH"]})
                self.assertEqual(snapshot(Path(temporary)), before, "mismatched packages mutated installation")

            def interrupt_and_recover(expected_snapshot):
                paused = Path(temporary) / "paused"
                process = start_process(
                    [str(a) for a in install],
                    env=env | {"POINTERACCEL_TEST_PAUSE_AFTER_MODULE": str(paused)},
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
                paused.unlink()
                self.assertTrue((system / "records/pending").exists())
                run(install, 1)
                run(install + ["--recover"])
                self.assertEqual(snapshot(Path(temporary)), expected_snapshot, "interrupted install recovery lost original files")

            interrupt_and_recover(before)
            paused = Path(temporary) / "committed-pause"
            process = start_process(
                [str(a) for a in install],
                env=env | {"POINTERACCEL_TEST_PAUSE_AFTER_COMMIT": str(paused)},
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
            paused.unlink()
            self.assertFalse((system / "records/pending").exists())
            run(install + ["--recover"])
            self.assertTrue((system / "plugins/pointeraccel_kwin.so").exists(), "recovery removed a committed module")
            run(uninstall)
            self.assertEqual(snapshot(Path(temporary)), before)
            run(install, 1, {"POINTERACCEL_TEST_FAIL_AFTER_MODULE": "1"})
            self.assertEqual(snapshot(Path(temporary)), before, "failed first install did not roll back")
            run(install)
            data = home / ".local/share/pointeraccel"
            notice = (data / "licenses/windows-pointer-linux.txt").read_bytes()
            # Preserve the original upstream notice byte-for-byte after moving it into README.
            self.assertEqual(hashlib.sha256(notice).hexdigest(), "6e32a6f6623073fc97f5d043c253e9ef566ac534ff438a2780f56abc0c740990")
            for name in ("README.md", "README.zh-CN.md"):
                document = (data / name).read_bytes()
                self.assertEqual(document, (source / name).read_bytes())
                self.assertTrue(notice in document)
            cli = home / ".local/bin/pointeraccel"
            run([cli, "config", "set", "pointer-speed", "14/20"])
            run([cli, "config", "set", "enhance-pointer-precision", "off"])
            self.assertEqual(json.loads(run([cli, "config", "get", "--json"]))["pointer_speed"], 14)
            run([cli, "config", "set", "pointer-speed", "21/20"], 2)
            gui = home / ".local/share/pointeraccel/bin/pointeraccel-gui"
            self.assertTrue(gui.is_file())
            saved_config = (home / ".config/pointeraccel/config.json").read_bytes()
            # Runtime font caches are not installation-owned files.
            with tempfile.TemporaryDirectory(prefix="pointeraccel-gui-cache-") as gui_cache:
                process = start_process(
                    [cli, "gui"],
                    env=env | {"QT_QPA_PLATFORM": "offscreen", "XDG_CACHE_HOME": gui_cache},
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
                    self.assertEqual(Path(f"/proc/{process.pid}/exe").resolve(), gui, "CLI did not replace itself with the GUI")
                    time.sleep(0.2)
                    self.assertTrue(process.poll() is None, "installed QWidget GUI failed to start")
                finally:
                    process.terminate()
                    process.communicate(timeout=10)
            self.assertEqual((home / ".config/pointeraccel/config.json").read_bytes(), saved_config)
            installed = snapshot(Path(temporary))
            interrupt_and_recover(installed)
            run(install, 1, {"POINTERACCEL_TEST_FAIL_AFTER_MODULE": "1"})
            self.assertEqual(snapshot(Path(temporary)), installed, "failed upgrade did not restore previous installation")
            run(install, 1, {"POINTERACCEL_TEST_FAIL_BEFORE_COMMIT": "1"})
            self.assertEqual(snapshot(Path(temporary)), installed, "late upgrade failure did not restore both manifests and services")
            run(install)
            self.assertEqual(json.loads(run([cli, "config", "get", "--json"]))["pointer_speed"], 14)
            # Recreate the legacy layout (direct KWin autostart, two-column ownership,
            # no compatibility record/service/snapshot) and migrate without replacing
            # the original backup or the user's settings.
            state = home / ".local/state/pointeraccel"
            data = home / ".local/share/pointeraccel"
            backup = (state / "backups/kwin.json").read_bytes()
            removed = [
                data / "bin/pointeraccel-gui",
                data / "source.tar.gz",
                data / "scripts/maintenance.py",
                state / "session.json",
                home / ".config/systemd/user/pointeraccel-session.service",
                home / ".config/systemd/user/plasma-workspace.target.wants/pointeraccel-session.service",
            ]
            manifest = state / "manifest.tsv"
            manifest.write_text(
                ''.join(
                    line + '\n'
                    for line in manifest.read_text().splitlines()
                    if line.split('\t')[-1] not in {str(path) for path in removed}
                )
            )
            for path in removed:
                path.unlink()
            (system / "records/compatibility.json").unlink()
            owner = system / "records/owner.tsv"
            owner.write_text('\t'.join(owner.read_text().strip().split('\t')[:2]) + '\n')
            digest = run([cli, "internal-plugin", "enable"]).strip()
            (state / "autostart.sha256").write_text(digest + '\n')
            run(install)
            self.assertTrue(gui.is_file(), "upgrade from a CLI-only installation did not add the GUI")
            self.assertEqual((state / "backups/kwin.json").read_bytes(), backup)
            self.assertTrue(b"pointeraccel_kwinEnabled=false" in kwin.read_bytes())
            self.assertEqual(json.loads(run([cli, "config", "get", "--json"]))["pointer_speed"], 14)
            run(uninstall)
            self.assertEqual(kwin.read_bytes(), original, "kwinrc was not restored byte-for-byte")
            self.assertEqual(snapshot(Path(temporary)), before, (
                "uninstall left owned files",
                set(snapshot(Path(temporary))) - set(before),
            ))
            run(uninstall)

    def test_preserves_preexisting_configuration(self):
        with Sandbox(build, self._testMethodName) as sandbox:
            temporary = sandbox.root
            home = Path(temporary) / "home"
            system = Path(temporary) / "system"
            env = sandbox.env | {
                "HOME": str(home),
                "XDG_CONFIG_HOME": str(home / ".config"),
                "XDG_DATA_HOME": str(home / ".local/share"),
                "XDG_STATE_HOME": str(home / ".local/state"),
                "XDG_CACHE_HOME": str(home / ".cache"),
                "DBUS_SESSION_BUS_ADDRESS": "unix:path=/nonexistent-pointeraccel-test",
                "POINTERACCEL_TEST_NO_BUILD": "1",
                "POINTERACCEL_ISOLATED_TEST": "1",
                "POINTERACCEL_TEST_SYSTEM_ROOT": str(system),
            }
            kwin = home / ".config/kwinrc"
            kwin.parent.mkdir(parents=True)
            original = b"[Windows]\nFocusPolicy=ClickToFocus\n\n[Plugins]\notherPluginEnabled=true\n"
            kwin.write_bytes(original)

            def run(arguments, expected=0, extra=None):
                return sandbox.run(arguments, env=env | (extra or {}), expected=expected, timeout=15)

            install = ["bash", source / "install.sh", "--test-system", system, "--build-dir", build]
            uninstall = ["bash", source / "uninstall.sh", "--offline", "--test-system", system]
            before = snapshot(Path(temporary))

            # Preserve new, unrelated KWin changes and a configuration that predates installation.
            config = home / ".config/pointeraccel/config.json"
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
            run(install)
            kwin.write_bytes(kwin.read_bytes() + b"\n[UserChange]\nKeepThis=true\n")
            run(uninstall)
            self.assertTrue(b"KeepThis=true" in kwin.read_bytes() and b"pointeraccel_kwinEnabled" not in kwin.read_bytes())
            self.assertEqual(config.read_bytes(), original_config)

    def test_modified_file_blocks_uninstall(self):
        with Sandbox(build, self._testMethodName) as sandbox:
            temporary = sandbox.root
            home = Path(temporary) / "home"
            system = Path(temporary) / "system"
            env = sandbox.env | {
                "HOME": str(home),
                "XDG_CONFIG_HOME": str(home / ".config"),
                "XDG_DATA_HOME": str(home / ".local/share"),
                "XDG_STATE_HOME": str(home / ".local/state"),
                "XDG_CACHE_HOME": str(home / ".cache"),
                "DBUS_SESSION_BUS_ADDRESS": "unix:path=/nonexistent-pointeraccel-test",
                "POINTERACCEL_TEST_NO_BUILD": "1",
                "POINTERACCEL_ISOLATED_TEST": "1",
                "POINTERACCEL_TEST_SYSTEM_ROOT": str(system),
            }
            kwin = home / ".config/kwinrc"
            kwin.parent.mkdir(parents=True)
            original = b"[Windows]\nFocusPolicy=ClickToFocus\n\n[Plugins]\notherPluginEnabled=true\n"
            kwin.write_bytes(original)

            def run(arguments, expected=0, extra=None):
                return sandbox.run(arguments, env=env | (extra or {}), expected=expected, timeout=15)

            install = ["bash", source / "install.sh", "--test-system", system, "--build-dir", build]
            uninstall = ["bash", source / "uninstall.sh", "--offline", "--test-system", system]
            before = snapshot(Path(temporary))

            # A changed installed file must be detected before any uninstall mutation.
            run(install)
            document = home / ".local/share/pointeraccel/README.md"
            document.write_text("changed by user")
            conflicted = snapshot(Path(temporary))
            run(uninstall, 1)
            self.assertEqual(snapshot(Path(temporary)), conflicted)


if __name__ == "__main__":
    test_main(build)
