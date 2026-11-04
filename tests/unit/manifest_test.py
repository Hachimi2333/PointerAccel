"""One contract corpus drives the Python, Shell and native installation validators."""
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'support'))
from harness import SOURCE, Sandbox, digest, test_main

BUILD = Path(sys.argv[1]).resolve()


class ManifestContract(unittest.TestCase):
    def test_all_entrypoints_agree(self):
        cases = json.loads((SOURCE / 'tests/fixtures/manifest_contract.json').read_text())
        for case in cases:
            with self.subTest(case=case['name']), Sandbox(BUILD, 'manifest-contract') as sandbox:
                data = Path(sandbox.env['XDG_DATA_HOME']) / 'pointeraccel'
                state = Path(sandbox.env['XDG_STATE_HOME']) / 'pointeraccel'
                program = data / 'bin/pointeraccel'
                cli = sandbox.home / '.local/bin/pointeraccel'
                required = [program, state / 'owner', state / 'system.sha256', state / 'backups/kwin.json']
                for path in required:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(str(os.getuid()) + '\n' if path.name == 'owner' else path.name)
                cli.parent.mkdir(parents=True)
                cli.symlink_to(program)
                rows = [f'F\t{digest(path)}\t{path}' for path in required]
                rows.append(f'L\t{program}\t{cli}')
                mutation = case['mutation']
                if mutation == 'empty':
                    rows = []
                elif mutation == 'missing_required':
                    rows.pop(0)
                elif mutation == 'duplicate':
                    rows.append(rows[0])
                elif mutation == 'unknown_kind':
                    rows[0] = rows[0].replace('F\t', 'X\t', 1)
                elif mutation == 'missing_field':
                    rows[0] = 'F\t' + str(program)
                elif mutation == 'extra_field':
                    rows[0] += '\textra'
                elif mutation == 'relative_path':
                    rows[0] = 'F\t' + digest(program) + '\trelative/path'
                elif mutation == 'outside':
                    outside = sandbox.root / 'unowned'
                    outside.write_text('unowned')
                    rows.append(f'F\t{digest(outside)}\t{outside}')
                elif mutation in ('parent_suffix', 'dot_suffix', 'directory_marker', 'duplicate_directory'):
                    path = str(data) + {'parent_suffix': '/..', 'dot_suffix': '/.'}.get(mutation, '')
                    marker = 'bad' if mutation == 'directory_marker' else '-'
                    rows.append(f'D\t{marker}\t{path}')
                    if mutation == 'duplicate_directory':
                        rows.append(rows[-1])
                elif mutation == 'optional_file':
                    optional = data / 'README.md'
                    optional.write_text('optional')
                    rows.append(f'F\t{digest(optional)}\t{optional}')
                elif mutation == 'corrupt':
                    program.write_text('changed')
                elif mutation == 'absent':
                    program.unlink()
                elif mutation == 'file_symlink':
                    target = sandbox.root / 'replacement'
                    program.rename(target)
                    program.symlink_to(target)
                elif mutation in ('directory_symlink', 'license_symlink'):
                    path = data / ('bin' if mutation == 'directory_symlink' else 'licenses')
                    if not path.exists():
                        path.mkdir()
                    target = sandbox.root / 'replacement-directory'
                    path.rename(target)
                    path.symlink_to(target, target_is_directory=True)
                elif mutation in ('relative_link', 'wrong_link'):
                    cli.unlink()
                    cli.symlink_to(os.path.relpath(program, cli.parent) if mutation == 'relative_link' else '/wrong')
                elif mutation == 'wrong_owner':
                    (state / 'owner').write_text(str(os.getuid() + 1))
                manifest = state / 'manifest.tsv'
                manifest.write_text('\n'.join(rows) + ('\n' if rows else ''))
                if mutation == 'manifest_symlink':
                    target = sandbox.root / 'saved-manifest'
                    manifest.rename(target)
                    manifest.symlink_to(target)
                commands = [
                    [sys.executable, SOURCE / 'scripts/maintenance.py', 'verify-installation'],
                    [BUILD / 'pa_manifest_probe'],
                    ['bash', '-c', 'source "$1/scripts/paths.sh"; pa_verify_manifest', 'contract', SOURCE],
                ]
                for command in commands:
                    result = subprocess.run([str(a) for a in command], env=sandbox.env,
                                            capture_output=True, text=True, timeout=5)
                    self.assertEqual(result.returncode, 0 if case['valid'] else 1,
                                     (case['name'], command, result.stdout, result.stderr))


if __name__ == '__main__':
    test_main(BUILD)
