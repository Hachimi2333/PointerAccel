"""Set HOME/XDG and private D-Bus before constructing the Qt application."""
from pathlib import Path
import subprocess
import sys
from harness import SOURCE, Sandbox, start_process, stop

build = Path(sys.argv[1]).resolve()
with Sandbox(build, Path(sys.argv[2]).name) as sandbox:
    sandbox.env['POINTERACCEL_TEST_ARTIFACTS'] = str(sandbox.artifacts)
    with (sandbox.artifacts / 'qt.log').open('w+') as log:
        process = start_process(['dbus-run-session', '--config-file', SOURCE / 'tests/support/session-bus.conf',
                                 '--', *sys.argv[2:]], env=sandbox.env, stdout=log, stderr=log)
        try:
            result = process.wait(timeout=80)
        except subprocess.TimeoutExpired:
            result = 124
        finally:
            stop(process)
        if result:
            log.seek(0)
            print(log.read())
            print(f'Test artifacts: {sandbox.artifacts}')
        sys.exit(result)
