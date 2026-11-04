#!/usr/bin/env python3
"""Isolated KWin integration test; never connects to the desktop's D-Bus session."""

import ast
import json
import os
import signal
import selectors
from pathlib import Path
import subprocess
import sys
import tempfile
import time

import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'support'))
from harness import Sandbox, dbus_call, start_process, stop, test_main, wait_for
from continuous_input import exercise
BUILD = Path(sys.argv[1]).resolve()


class NestedInput(unittest.TestCase):
    def test_input_pipeline(self):
        build = BUILD
        with Sandbox(build, self._testMethodName) as sandbox:
            temporary = sandbox.root
            env = sandbox.env.copy()
            for variable, folder in [
                ("XDG_CONFIG_HOME", "config"),
                ("XDG_DATA_HOME", "data"),
                ("XDG_STATE_HOME", "state"),
                ("XDG_CACHE_HOME", "cache"),
            ]:
                env[variable] = str(Path(temporary) / folder)
            env["POINTERACCEL_ISOLATED_TEST"] = "1"
            env["QT_PLUGIN_PATH"] = str(build / "teststage/qt6/plugins")
            initial_kwin = Path(env["XDG_CONFIG_HOME"]) / "kwinrc"
            initial_kwin.parent.mkdir(parents=True, exist_ok=True)
            initial_kwin.write_text("[Plugins]\npointeraccel_kwinEnabled=true\n")
            log = open(sandbox.artifacts / "kwin.log", "w+")
            socket = f"pointeraccel-test-{os.getpid()}"
            compositor = start_process(
                [
                    "/usr/bin/kwin_wayland",
                    "--virtual",
                    "--socket",
                    socket,
                    "--width",
                    "1280",
                    "--height",
                    "800",
                    "--scale",
                    "1.25",
                    "--no-lockscreen",
                    "--no-global-shortcuts",
                    "--no-kactivities",
                    "--inputmethod",
                    "/bin/true",
                    "--xwayland",
                ],
                env=env,
                stdout=log,
                stderr=log,
            )

            def call(path, method, *args):
                interface, name = method.rsplit('.', 1)
                return dbus_call(env, path, interface, name, *args)

            def rpc(name, *args):
                return call("/org/kde/KWin/PointerAccel", "org.kde.KWin.PointerAccel1." + name, *args)

            try:
                for _ in range(100):
                    try:
                        call("/KWin", "org.kde.KWin.activeOutputName")
                        break
                    except RuntimeError:
                        if compositor.poll() is not None:
                            raise RuntimeError("Nested KWin exited")
                        time.sleep(0.1)
                else:
                    raise RuntimeError("Nested KWin startup timed out")
                env["WAYLAND_DISPLAY"] = socket
                with tempfile.TemporaryFile(mode="w+") as scale_log:
                    scale = subprocess.run(
                        ["kscreen-doctor", "output.Virtual-0.scale.1.25"],
                        env=env,
                        stdout=scale_log,
                        stderr=scale_log,
                        timeout=10,
                    )
                    scale_log.seek(0)
                    scale_text = scale_log.read()
                self.assertEqual(scale.returncode, 0, scale_text)
                time.sleep(0.1)
                # Exercise startup loading through the normal KWin plugin manager.
                self.assertTrue(rpc("Status")["loaded"])
                time.sleep(0.1)
                self.assertTrue(rpc("Status")["active"], rpc("Status"))
                base = time.monotonic_ns() // 1000
                reports = [
                    {"device": i % 2, "x": x, "y": y, "dx": x * 3.125, "dy": y * 3.125, "time": base + i * 125}
                    for i, (x, y) in enumerate([(1, 1), (2, -1), (12, 5), (-4, 0), (-1, -1), (0, 0), (1, -2)])
                ]
                reports.insert(3, {"device": 2, "x": 0.25, "y": -0.5, "dx": 0.75, "dy": -1.25, "time": base + 300})
                observed = rpc("TestReports", json.dumps({"reports": reports}))
                self.assertEqual(observed["observed"], observed["expected"], observed)
                self.assertEqual(rpc("Diagnostics")["processed"], len(reports) - 1)
                self.assertEqual(rpc("Diagnostics")["last"]["dpi"], 120, rpc("Diagnostics"))
                self.assertTrue(rpc("TestHotplug")["active"])
                replugged = rpc("TestReports", json.dumps({"reports": reports[:2]}))
                self.assertEqual(replugged["observed"], replugged["expected"])
                self.assertFalse(rpc("SetTemporaryEnabled", "false")["active"])
                native = rpc("TestReports", json.dumps({"reports": reports[:2]}))
                self.assertEqual(len(native["observed"]), 1, native)
                self.assertEqual(native["observed"][0]["raw_x"], sum(r["x"] for r in reports[:2]))
                self.assertTrue(rpc("SetTemporaryEnabled", "true")["active"])
                corrupt = rpc("TestReports", json.dumps({"reports": reports[:2], "corrupt": True}))
                self.assertTrue(not corrupt["status"]["active"] and not corrupt["status"]["healthy"], corrupt)
                self.assertEqual(len(corrupt["observed"]), 1, corrupt)
                shutdown = rpc("Shutdown")
                self.assertTrue(shutdown["healthy"] and not shutdown["active"], shutdown)
                self.assertTrue(rpc("ApplyConfig")["active"])
                protocol_reports = [r for r in reports if r["device"] != 2 and (r["x"] or r["y"])]
                client_env = env | {k: v for k, v in rpc("TestEnvironment").items() if v}
                client_env["WAYLAND_DISPLAY"] = socket
                for client in ["pointeraccel-raw-wayland", "pointeraccel-raw-x11"]:
                    probe = start_process(
                        [str(build / client)], env=client_env, stdout=subprocess.PIPE, stderr=log, bufsize=0
                    )
                    selector = selectors.DefaultSelector()
                    selector.register(probe.stdout, selectors.EVENT_READ)
                    try:
                        self.assertTrue(selector.select(8), f"{client} did not start")
                        self.assertEqual(probe.stdout.readline().strip(), b"READY", f"{client} startup failed")
                        time.sleep(0.2)
                        rpc("TestReports", json.dumps({"reports": protocol_reports}))
                        samples = []
                        deadline = time.monotonic() + 3
                        # XWayland may batch events; the raw sum must remain unchanged.
                        while time.monotonic() < deadline:
                            if not selector.select(0.1):
                                if samples:
                                    break
                                continue
                            line = probe.stdout.readline()
                            if not line:
                                break
                            samples.append(json.loads(line))
                        for axis in ["x", "y"]:
                            self.assertEqual(sum(s["raw_" + axis] for s in samples), sum(r[axis] for r in protocol_reports), (
                                client,
                                samples,
                            ))
                        samples = [
                            s for s in samples if s["raw_x"] or s["raw_y"]
                        ]  # The explicit test warp sends a zero marker.
                        self.assertEqual([(s["raw_x"], s["raw_y"]) for s in samples], [
                            (r["x"], r["y"]) for r in protocol_reports
                        ], (client, samples))
                    finally:
                        probe.terminate()
                        try:
                            probe.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            probe.kill()
                            probe.wait()
                        selector.close()
                        probe.stdout.close()
                config_path = Path(env["XDG_CONFIG_HOME"]) / "pointeraccel/config.json"
                config_path.parent.mkdir(parents=True, exist_ok=True)
                configuration = {
                    "enabled": True,
                    "pointer_speed": 14,
                    "enhance_pointer_precision": False,
                    "excluded_devices": [],
                    "trace": True,
                }
                config_path.write_text(json.dumps(configuration))
                self.assertTrue(rpc("ApplyConfig")["active"])
                many = [
                    {"device": i % 2, "x": 1, "y": -1, "dx": 3.125, "dy": -3.125, "time": base + 10000 + i * 125}
                    for i in range(5000)
                ]
                for offset in range(0, len(many), 500):
                    traced = rpc("TestReports", json.dumps({"reports": many[offset : offset + 500]}))
                    self.assertEqual(traced["observed"], traced["expected"])
                self.assertEqual(rpc("Diagnostics")["trace_samples"], 4096)
                first = next(d for d in rpc("Devices")["devices"] if d["name"] == "pa-test-0")
                configuration["excluded_devices"] = [first["id"]]
                configuration["trace"] = False
                config_path.write_text(json.dumps(configuration))
                rpc("ApplyConfig")
                excluded = rpc("TestReports", json.dumps({"reports": reports[:2]}))
                self.assertEqual(excluded["observed"], excluded["expected"])
                self.assertEqual(rpc("Diagnostics")["trace_samples"], 0)
                # The GUI commits through the shared controller and exits without touching module lifetime.
                gui = subprocess.run(
                    [str(build / "pa_gui_driver")],
                    env=client_env,
                    stdout=log,
                    stderr=log,
                    timeout=20,
                )
                self.assertEqual(gui.returncode, 0, f"GUI failed to apply/close: {gui.returncode}")
                gui_status = json.loads(
                    subprocess.check_output([str(build / "pointeraccel"), "status", "--json"], env=env, text=True)
                )
                self.assertTrue(gui_status["loaded"] and gui_status["active"])
                self.assertEqual(gui_status["settings"]["pointer_speed"], 15)
                self.assertFalse(gui_status["settings"]["enhance_pointer_precision"])
                after_gui = rpc("TestReports", json.dumps({"reports": reports[:2]}))
                self.assertEqual(after_gui["observed"], after_gui["expected"])
                fixture = json.loads((Path(__file__).parents[1] / "fixtures/continuous_input.json").read_text())
                def set_scale(value):
                    subprocess.run(["kscreen-doctor", f"output.Virtual-0.scale.{value}"], env=env,
                                   check=True, capture_output=True, timeout=10)
                continuous = exercise(rpc, config_path, fixture["actions"], set_scale)
                self.assertEqual(continuous, fixture["observations"], ("continuous input diverged from 7f67730", continuous))
                call("/Plugins", "org.kde.KWin.Plugins.UnloadPlugin", "pointeraccel_kwin")
                self.assertTrue(compositor.poll() is None)
                print(
                    "nested KWin: load, RELRO interception, per-report motion, shared mouse state, touchpad passthrough, 125% scale, disable, mismatch fallback, Wayland/XWayland raw motion, re-enable, unload: PASS"
                )
            except Exception:
                log.flush()
                log.seek(0)
                print(log.read(), file=sys.stderr)
                raise
            finally:
                compositor.terminate()
                try:
                    compositor.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    compositor.kill()
                    compositor.wait()
                log.close()

if __name__ == "__main__":
    test_main(BUILD, private_bus=True)
