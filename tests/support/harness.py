"""Isolated system-test resources. No dependency beyond the Python standard library."""
import atexit
import ast
from contextlib import contextmanager
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest
import uuid

SOURCE = Path(__file__).resolve().parents[2]
_processes = []


def stop(process, timeout=5):
    """Stop the whole owned process group, including grandchildren, then reap."""
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait(timeout=5)
    # The leader can exit before its children.
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    if process in _processes:
        _processes.remove(process)


def start_process(arguments, **kwargs):
    process = subprocess.Popen([str(a) for a in arguments], start_new_session=True, **kwargs)
    _processes.append(process)
    return process


def _cleanup():
    for process in list(reversed(_processes)):
        stop(process)


atexit.register(_cleanup)


def wait_for(predicate, message, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.05)
    raise AssertionError(message)


def run(arguments, *, env=None, expected=0, timeout=20, log=None):
    result = subprocess.run([str(a) for a in arguments], env=env, capture_output=True, text=True, timeout=timeout)
    if log:
        with Path(log).open('a') as stream:
            stream.write(f'$ {arguments!r}\n{result.stdout}{result.stderr}\nexit={result.returncode}\n')
    if result.returncode != expected:
        raise AssertionError((arguments, result.returncode, result.stdout, result.stderr))
    return result.stdout


def dbus_call(env, path, interface, method, *arguments):
    output = run([
        'gdbus', 'call', '--session', '--dest', 'org.kde.KWin',
        '--object-path', path, '--method', interface + '.' + method, *arguments,
    ], env=env, timeout=10).strip()
    if output == '()':
        return None
    if output in ('(true,)', '(false,)'):
        return output == '(true,)'
    value = ast.literal_eval(output)[0]
    return json.loads(value) if isinstance(value, str) and value.startswith('{') else value


class Sandbox:
    def __init__(self, build, name):
        self.artifacts = Path(build) / 'test-artifacts' / name / uuid.uuid4().hex[:12]
        self.artifacts.mkdir(parents=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='pa-test-')
        self.root = Path(self.temporary.name)
        self.home = self.root / 'home'
        self.system = self.root / 'system'
        self.env = os.environ.copy()
        for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'QT_PLUGIN_PATH', 'XAUTHORITY', 'SESSION_MANAGER'):
            self.env.pop(key, None)
        self.env.update(QT_QPA_PLATFORMTHEME='generic', GTK_USE_PORTAL='0',
                        HOME=str(self.home), POINTERACCEL_ISOLATED_TEST='1',
                        POINTERACCEL_TEST_SYSTEM_ROOT=str(self.system))
        for key, folder in [('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'),
                            ('XDG_STATE_HOME', 'state'), ('XDG_CACHE_HOME', 'cache'),
                            ('XDG_RUNTIME_DIR', 'runtime')]:
            path = self.root / folder
            path.mkdir(mode=0o700)
            self.env[key] = str(path)
        self.home.mkdir()

    def __enter__(self):
        return self

    def __exit__(self, kind, value, traceback):
        self.temporary.cleanup()
        if kind:
            print(f'Test artifacts: {self.artifacts}', file=sys.stderr)

    def run(self, arguments, **kwargs):
        return run(arguments, env=kwargs.pop('env', self.env), log=self.artifacts / 'commands.log', **kwargs)

    @contextmanager
    def compositor(self, plugin_root, *, outputs=1, xwayland=False, scale=1):
        self.env['QT_PLUGIN_PATH'] = str(plugin_root)
        socket = 'pa-test-' + uuid.uuid4().hex[:12]
        self.env['WAYLAND_DISPLAY'] = socket
        arguments = ['/usr/bin/kwin_wayland', '--virtual', '--socket', socket,
                     '--width', '640', '--height', '480', '--output-count', str(outputs),
                     '--scale', str(scale), '--no-lockscreen', '--no-global-shortcuts',
                     '--no-kactivities', '--inputmethod', '/bin/true']
        if xwayland:
            arguments.append('--xwayland')
        with (self.artifacts / 'kwin.log').open('a') as log:
            process = start_process(arguments, env=self.env, stdout=log, stderr=log)
            try:
                def ready():
                    if process.poll() is not None:
                        raise AssertionError(f'Nested KWin exited: {process.returncode}; see {self.artifacts}')
                    result = subprocess.run(['gdbus', 'introspect', '--session', '--dest', 'org.kde.KWin',
                                             '--object-path', '/Plugins'], env=self.env,
                                            capture_output=True, timeout=3)
                    return result.returncode == 0 and b'LoadPlugin' in result.stdout
                wait_for(ready, 'Nested KWin did not become ready')
                yield process
            finally:
                stop(process)

    def rpc(self, method, *arguments):
        return dbus_call(self.env, '/org/kde/KWin/PointerAccel', 'org.kde.KWin.PointerAccel1', method, *arguments)


def test_main(build, *, private_bus=False):
    """Keep unittest selection arguments, isolate buses, and clean up on CTest SIGTERM."""
    def terminate(signum, frame):
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, terminate)
    arguments = sys.argv[2:]
    if private_bus and '--inside' not in arguments:
        with Sandbox(build, 'private-bus') as bootstrap:
            process = start_process(['dbus-run-session', '--config-file', str(SOURCE / 'tests/support/session-bus.conf'),
                                     '--', sys.executable, sys.argv[0], str(build),
                                     '--inside', *arguments], env=bootstrap.env)
            try:
                result = process.wait(timeout=170)
            finally:
                stop(process)
        raise SystemExit(result)
    arguments = [arg for arg in arguments if arg != '--inside']
    unittest.main(argv=[sys.argv[0], *arguments])
