#!/usr/bin/env python3
"""Exercise the real guard, repair and module factory on a private D-Bus bus."""

import ast
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time

import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'support'))
from harness import Sandbox, dbus_call, start_process, stop, test_main, wait_for, script, snapshot, digest

SOURCE = Path(__file__).resolve().parents[2]
BUILD = Path(sys.argv[1]).resolve()



class UpgradeGuard(unittest.TestCase):
    def test_guard_and_repair(self):
        with Sandbox(BUILD, self._testMethodName) as sandbox:
            temporary = sandbox.root
            root = Path(temporary)
            home = root / 'home'
            system = root / 'system'
            source = root / 'source'
            shutil.copytree(
                SOURCE,
                source,
                ignore=lambda directory, names: [
                    name
                    for name in names
                    if name in ('.git', '__pycache__', '.test-output')
                    or (Path(directory) == SOURCE and name.startswith('build'))
                ],
            )
            env = sandbox.env | {
                'HOME': str(home),
                'XDG_CONFIG_HOME': str(home / '.config'),
                'XDG_DATA_HOME': str(home / '.local/share'),
                'XDG_STATE_HOME': str(home / '.local/state'),
                'XDG_CACHE_HOME': str(home / '.cache'),
                'POINTERACCEL_ISOLATED_TEST': '1',
                'POINTERACCEL_TEST_SYSTEM_ROOT': str(system),
                'POINTERACCEL_TEST_NO_BUILD': '1',
                'POINTERACCEL_TEST_BUILD_DIR': str(BUILD),
                'XDG_SESSION_ID': 'upgrade-test-new-session',
            }
            install_env = env | {
                'DBUS_SESSION_BUS_ADDRESS': 'unix:path=/nonexistent-pointeraccel-upgrade',
                'XDG_SESSION_ID': 'upgrade-test-install-session',
            }
            cli = home / '.local/bin/pointeraccel'
            state = home / '.local/state/pointeraccel'
            data = home / '.local/share/pointeraccel'
            manifest = state / 'manifest.tsv'
            record = system / 'records/compatibility.json'
            owner = system / 'records/owner.tsv'
            module = system / 'plugins/pointeraccel_kwin.so'

            def run(args, expected=0, environment=None, timeout=20):
                return sandbox.run(args, env=environment or env, expected=expected, timeout=timeout)

            install = ['bash', source / 'install.sh', '--test-system', system, '--build-dir', BUILD]
            run(install, environment=install_env)
            self.assertTrue(b'pointeraccel_kwinEnabled=false' in (home / '.config/kwinrc').read_bytes())
            self.assertTrue((home / '.config/systemd/user/plasma-workspace.target.wants/pointeraccel-session.service').is_symlink())
            saved_record, saved_owner = record.read_bytes(), owner.read_bytes()

            def set_record(value):
                record.write_text(json.dumps(value))
                owner.write_text(f'{os.getuid()}\t{digest(module)}\t{digest(record)}\n')

            def own(path):
                lines = manifest.read_text().splitlines()
                manifest.write_text(
                    '\n'.join(f'F\t{digest(path)}\t{path}' if line.split('\t')[-1] == str(path) else line for line in lines)
                    + '\n'
                )

            def startup():
                return json.loads(run([cli, 'internal-startup-status']))

            def status():
                return json.loads(run([cli, 'status', '--json']))

            def call(path, interface, method, *args):
                return dbus_call(env, path, interface, method, *args)

            def loaded():
                output = run(
                    [
                        'gdbus',
                        'call',
                        '--session',
                        '--dest',
                        'org.kde.KWin',
                        '--object-path',
                        '/Plugins',
                        '--method',
                        'org.freedesktop.DBus.Properties.Get',
                        'org.kde.KWin.Plugins',
                        'LoadedPlugins',
                    ]
                )
                return "'pointeraccel_kwin'" in output

            log = (sandbox.artifacts / 'kwin.log').open('w+')

            def compositor(plugin_path):
                return start_process(
                    [
                        '/usr/bin/kwin_wayland',
                        '--virtual',
                        '--socket',
                        f'pa-upgrade-{os.getpid()}',
                        '--width',
                        '800',
                        '--height',
                        '600',
                        '--no-lockscreen',
                        '--no-global-shortcuts',
                        '--no-kactivities',
                        '--inputmethod',
                        '/bin/true',
                    ],
                    env=env | {'QT_PLUGIN_PATH': str(plugin_path)},
                    stdout=log,
                    stderr=log,
                )

            def ready(process):
                def check():
                    self.assertTrue(process.poll() is None, 'nested KWin exited')
                    result = subprocess.run(
                        ['gdbus', 'introspect', '--session', '--dest', 'org.kde.KWin', '--object-path', '/Plugins'],
                        env=env,
                        capture_output=True, timeout=3,
                    )
                    return result.returncode == 0 and b'LoadPlugin' in result.stdout

                wait_for(check, 'nested KWin did not become ready')

            # Wrong runtime Build ID is rejected by the factory before construction.
            kwin = compositor(BUILD / 'mismatchstage/qt6/plugins')
            watcher = None
            try:
                ready(kwin)
                self.assertFalse(call('/Plugins', 'org.kde.KWin.Plugins', 'LoadPlugin', 'pointeraccel_kwin'))
                self.assertTrue(not loaded() and kwin.poll() is None)
                stop(kwin)
                wait_for(
                    lambda: subprocess.run(
                        ['gdbus', 'introspect', '--session', '--dest', 'org.kde.KWin', '--object-path', '/Plugins'],
                        env=env,
                        capture_output=True, timeout=3,
                    ).returncode
                    != 0,
                    'old D-Bus owner remained',
                )
                # Only our fake system module is visible on the private compositor.
                plugin_root = root / 'qt'
                (plugin_root / 'kwin').mkdir(parents=True)
                (plugin_root / 'kwin/plugins').symlink_to(system / 'plugins')
                kwin = compositor(plugin_root)
                ready(kwin)
                self.assertFalse(loaded())

                wrong = json.loads(saved_record)
                wrong['executable_sha256'] = '0' * 64
                set_record(wrong)
                watcher = start_process([cli, 'internal-watch'], env=env, stdout=log, stderr=log)
                self.assertEqual(startup()['startup_state'], 'repair-required')
                time.sleep(0.45)
                self.assertFalse(loaded())
                # Install marker prevents loading into the same login/DBus instance.
                run([cli, 'internal-session-stamp', state / 'session.json'])
                own(state / 'session.json')
                record.write_bytes(saved_record)
                owner.write_bytes(saved_owner)
                self.assertEqual(startup()['startup_state'], 'pending-login')
                time.sleep(0.45)
                self.assertFalse(loaded())

                # Missing/corrupt binaries, records, manifests and pending transactions.
                original_module = module.read_bytes()
                module.write_bytes(original_module + b'changed')
                self.assertEqual(startup()['startup_state'], 'blocked')
                time.sleep(0.45)
                self.assertFalse(loaded())
                module.write_bytes(original_module)
                absent = root / 'module-backup.so'
                module.rename(absent)
                self.assertEqual(startup()['startup_state'], 'blocked')
                time.sleep(0.45)
                self.assertFalse(loaded())
                absent.rename(module)
                record.write_text('{bad json')
                self.assertEqual(startup()['startup_state'], 'blocked')
                time.sleep(0.45)
                self.assertFalse(loaded())
                record.write_bytes(saved_record)
                pending = system / 'records/pending'
                pending.write_text(str(os.getuid()))
                self.assertEqual(startup()['startup_state'], 'blocked')
                time.sleep(0.45)
                self.assertFalse(loaded())
                pending.unlink()
                original_manifest = manifest.read_bytes()
                manifest.write_text('invalid\n')
                self.assertEqual(startup()['startup_state'], 'blocked')
                time.sleep(0.45)
                self.assertFalse(loaded())
                manifest.write_bytes(original_manifest)

                # Supported but different builds need repair; other series remain unsupported.
                # Neither case may issue a load request.
                stop(watcher)
                watcher = None
                fake_bin = root / 'bin'
                fake_bin.mkdir()
                fake_rpm = fake_bin / 'rpm'
                unsupported_env = env | {'PATH': str(fake_bin) + ':' + env['PATH']}
                installed_version = json.loads(saved_record)['kwin_version']
                repair_versions = [
                    f'{series}.{int(installed_version.split(".")[-1]) + 1}' for series in ('6.7', '6.8')
                ]
                for version, expected in [
                    *((version, 'repair-required') for version in repair_versions),
                    ('6.6.9', 'unsupported'),
                    ('6.9.0', 'unsupported'),
                    ('7.0.0', 'unsupported'),
                ]:
                    fake_rpm.write_text(f'#!/bin/sh\nprintf {version}\n')
                    fake_rpm.chmod(0o755)
                    self.assertEqual(json.loads(run([cli, 'internal-startup-status'], environment=unsupported_env))['startup_state'], expected)
                    watcher = start_process([cli, 'internal-watch'], env=unsupported_env, stdout=log, stderr=log)
                    time.sleep(0.5)
                    self.assertFalse(loaded())
                    stop(watcher)
                    watcher = None

                # Model a fresh login; the guard is now the only way to load.
                run([cli, 'internal-session-stamp', state / 'session.json'])
                stamp = json.loads((state / 'session.json').read_text())
                stamp['owner'] = ':1.999999'
                # Retain the old session environment to model a lingering manager.
                (state / 'session.json').write_text(json.dumps(stamp))
                own(state / 'session.json')
                self.assertEqual(startup()['startup_state'], 'ready')
                watcher = start_process([cli, 'internal-watch'], env=env, stdout=log, stderr=log)
                wait_for(lambda: status()['active'], 'guard did not load the compatible plugin')
                self.assertTrue(status()['healthy'] and status()['running_compatible'])
                run([cli, 'doctor'])
                run([cli, 'disable'])
                self.assertTrue(status()['healthy'] and not status()['active'])
                run([cli, 'doctor'])
                run([cli, 'enable'])

                # A disk upgrade must not turn a working old runtime into a failure.
                set_record(wrong)
                upgraded = status()
                self.assertTrue(upgraded['active']
                    and upgraded['running_compatible']
                    and upgraded['repair_required']
                    and not upgraded['healthy'], upgraded)
                run([cli, 'config', 'set', 'pointer-speed', '12/20'])
                self.assertTrue(status()['active'] and status()['settings']['pointer_speed'] == 12)
                run([cli, 'doctor'], expected=1)
                record.write_bytes(saved_record)
                owner.write_bytes(saved_owner)

                # Repair works with the original source tree removed and does not
                # unload/reload the current module (a sentinel state survives).
                shutil.rmtree(source)
                run([cli, 'repair'], timeout=35)
                repaired = status()
                self.assertTrue(repaired['active'] and repaired['relogin_required'] and repaired['startup_state'] == 'pending-login', repaired)
                self.assertEqual(repaired['settings']['pointer_speed'], 12)
                self.assertFalse(source.exists())
                # Ordinary build/test failures and a package update mid-build must
                # leave both user and system installation bytes unchanged.
                for failure in ('configure', 'tests', 'upgrade'):
                    marker = root / 'rpm-changed'
                    if marker.exists():
                        marker.unlink()
                    fake_rpm.unlink(missing_ok=True)
                    for name in ('cmake', 'ctest'):
                        (fake_bin / name).unlink(missing_ok=True)
                    cmake = fake_bin / 'cmake'
                    cmake.write_text(
                        '#!/bin/sh\n'
                        + (
                            'exit 33\n'
                            if failure == 'configure'
                            else f'touch "{marker}"\nexit 0\n' if failure == 'upgrade' else 'exit 0\n'
                        )
                    )
                    cmake.chmod(0o755)
                    ctest = fake_bin / 'ctest'
                    ctest.write_text('#!/bin/sh\nexit ' + ('44' if failure == 'tests' else '0') + '\n')
                    ctest.chmod(0o755)
                    if failure == 'upgrade':
                        rpm = shutil.which('rpm')
                        fake_rpm.write_text(script(f'''#!/usr/bin/env python3
    import subprocess, sys
    result=subprocess.check_output([{rpm!r}]+sys.argv[1:]).decode()
    if __import__('pathlib').Path({str(marker)!r}).exists():
        result=result.replace('-', '-pointeraccel-test.', 1)
    print(result,end='')
    '''))
                        fake_rpm.chmod(0o755)
                    before = snapshot(home, system)
                    result = subprocess.run(
                        [cli, 'repair', '--source', SOURCE],
                        env=env | {'PATH': str(fake_bin) + ':' + env['PATH'], 'POINTERACCEL_TEST_NO_BUILD': '0'},
                        capture_output=True,
                        timeout=25,
                    )
                    self.assertTrue(result.returncode != 0, (failure, result.stdout, result.stderr))
                    self.assertEqual(snapshot(home, system), before, f'{failure} mutated installation')
                stop(watcher)
                watcher = None
                # Shutdown via the private session before removing the sandbox install.
                run([cli, 'internal-shutdown'])
                run(['bash', data / 'uninstall.sh', '--test-system', system])
                self.assertTrue(not module.exists() and not manifest.exists())
                self.assertTrue(kwin.poll() is None)
                print(
                    'upgrade guard: factory rejection, no-load failures, delayed loading, active old runtime, source-free repair, relogin gate, build/test/update failure preservation: PASS'
                )
            except Exception:
                log.flush()
                log.seek(0)
                print(log.read(), file=sys.stderr)
                raise
            finally:
                if watcher is not None:
                    stop(watcher)
                if kwin.poll() is None:
                    stop(kwin)
                log.close()

if __name__ == "__main__":
    test_main(BUILD, private_bus=True)
