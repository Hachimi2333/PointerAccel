#!/usr/bin/env python3
"""Data-only compatibility records and source snapshots; never imports user code."""

import fcntl
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import sys
import tarfile

EXECUTABLE = Path('/usr/bin/kwin_wayland')
LIBRARY = Path('/usr/lib64/libkwin.so.6')
SOURCE_ITEMS = (
    'CMakeLists.txt',
    'README.md',
    'LICENSE',
    'LICENSES',
    'include',
    'src',
    'scripts',
    'tests',
    'tools',
    'install.sh',
    'uninstall.sh',
)


def digest(path):
    with open(path, 'rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def elf_id(path):
    data = Path(path).read_bytes()
    if len(data) < 64 or data[:6] != b'\x7fELF\x02\x01':
        raise ValueError(f'不是受支持的 x86-64 ELF: {path}')
    offset = struct.unpack_from('<Q', data, 32)[0]
    size, count = struct.unpack_from('<HH', data, 54)
    if size != 56 or offset + size * count > len(data):
        raise ValueError(f'无效的 ELF 程序头: {path}')
    for index in range(count):
        kind, _, start, _, _, length, _, _ = struct.unpack_from('<IIQQQQQQ', data, offset + index * size)
        if kind != 4 or start + length > len(data):
            continue
        end = start + length
        while start + 12 <= end:
            names, desc, note = struct.unpack_from('<III', data, start)
            start += 12
            name_size, desc_size = (names + 3) & ~3, (desc + 3) & ~3
            if start + name_size + desc_size > end:
                break
            if note == 3 and data[start : start + names] == b'GNU\0':
                return data[start + name_size : start + name_size + desc].hex()
            start += name_size + desc_size
    raise ValueError(f'ELF 缺少 GNU Build ID: {path}')


def fingerprint():
    packages = {}
    versions = {}
    for package in ('kwin', 'kwin-libs', 'kwin-devel'):
        result = subprocess.run(
            ['rpm', '-q', '--qf', '%{VERSION}\t%{EPOCHNUM}:%{VERSION}-%{RELEASE}.%{ARCH}', package],
            text=True,
            capture_output=True,
            check=True,
        )
        versions[package], packages[package] = result.stdout.strip().split('\t')
    version = versions['kwin']
    if not re.fullmatch(r'6\.7\.\d+', version):
        raise ValueError(f'KWin {version} 尚未适配；支持系列为 6.7，需要代码审查，重新编译不能解决。')
    if len(set(packages.values())) != 1:
        raise ValueError('kwin、kwin-libs 和 kwin-devel 版本/发行号不匹配，请先完成系统更新。')
    library = LIBRARY.resolve(strict=True)
    return {
        'kwin_version': version,
        'kwin_library': str(library),
        'kwin_executable': str(EXECUTABLE),
        'kwin_sha256': digest(library),
        'executable_sha256': digest(EXECUTABLE),
        'kwin_build_id': elf_id(library),
        'executable_build_id': elf_id(EXECUTABLE),
        'packages': packages,
    }


def verify(record):
    current = fingerprint()
    for key, value in current.items():
        if record.get(key) != value:
            raise ValueError(f'构建期间 KWin 已变化或清单不匹配 ({key})，已停止；请重新运行修复。')


def write(path, record):
    Path(path).write_text(json.dumps(record, indent=2, ensure_ascii=False) + '\n')


def snapshot(source, destination):
    source = Path(source).resolve(strict=True)
    # Fixed allowlist, normalized tar metadata, no links, caches or build output.
    with open(destination, 'wb') as output, gzip.GzipFile(
        filename='', mode='wb', fileobj=output, mtime=0
    ) as compressed, tarfile.open(fileobj=compressed, mode='w') as archive:
        paths = []
        for item in SOURCE_ITEMS:
            path = source / item
            paths.append(path)
            if path.is_dir():
                paths.extend(sorted(path.rglob('*')))
        for path in paths:
            if '__pycache__' in path.parts or path.suffix == '.pyc':
                continue
            if path.is_symlink() or not (path.is_dir() or path.is_file()):
                raise ValueError(f'源码快照不接受链接或特殊文件: {path}')
            info = archive.gettarinfo(str(path), arcname=str(path.relative_to(source)))
            info.uid = info.gid = info.mtime = 0
            info.uname = info.gname = ''
            if path.is_file():
                with path.open('rb') as stream:
                    archive.addfile(info, stream)
            else:
                archive.addfile(info)


def extract(source, destination):
    root = Path(destination).resolve(strict=True)
    with tarfile.open(source) as archive:
        for member in archive.getmembers():
            path = root / member.name
            if (
                Path(member.name).is_absolute()
                or '..' in Path(member.name).parts
                or not (member.isfile() or member.isdir())
                or not path.resolve().is_relative_to(root)
            ):
                raise ValueError('源码快照包含无效路径或特殊文件。')
        archive.extractall(root, filter='data')


def lock_path():
    state = str(Path(os.environ.get('XDG_STATE_HOME', str(Path.home() / '.local/state'))) / 'pointeraccel')
    return f'/tmp/pointeraccel-{os.getuid()}-{hashlib.sha256(state.encode()).hexdigest()}.lock'


def user_paths():
    home = Path.home()
    config = Path(os.environ.get('XDG_CONFIG_HOME', home / '.config'))
    data = Path(os.environ.get('XDG_DATA_HOME', home / '.local/share')) / 'pointeraccel'
    state = Path(os.environ.get('XDG_STATE_HOME', home / '.local/state')) / 'pointeraccel'
    return {
        'data': str(data),
        'config': str(config / 'pointeraccel'),
        'state': str(state),
        'cli': str(home / '.local/bin/pointeraccel'),
        'kwin': str(config / 'kwinrc'),
        'unit': str(config / 'systemd/user/pointeraccel-session.service'),
        'unit-link': str(config / 'systemd/user/plasma-workspace.target.wants/pointeraccel-session.service'),
    }


def journal_path():
    return Path(user_paths()['state'] + '.transaction.json')


def load_journal():
    path = journal_path()
    if path.is_symlink() or path.stat().st_uid != os.getuid():
        raise ValueError('安装事务日志的所有者或类型无效。')
    record = json.loads(path.read_text())
    backup = Path(record['backup'])
    if (
        record['paths'] != user_paths()
        or backup.is_symlink()
        or not backup.is_dir()
        or backup.stat().st_uid != os.getuid()
        or backup.parent != Path(user_paths()['state']).parent
        or not backup.name.startswith('pointeraccel.backup.')
    ):
        raise ValueError('安装事务日志或备份路径无效。')
    return record


def recover_user(action):
    record = load_journal()
    backup = Path(record['backup'])
    if action == 'restore':
        for name, value in record['paths'].items():
            destination = Path(value)
            if destination.is_symlink() or destination.is_file():
                destination.unlink()
            elif destination.exists():
                shutil.rmtree(destination)
            saved = backup / name
            if saved.is_symlink():
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.symlink_to(os.readlink(saved))
            elif saved.is_dir():
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copytree(saved, destination, symlinks=True, copy_function=shutil.copy2)
            elif saved.is_file():
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(saved, destination)
    elif action != 'keep':
        raise ValueError('无效的恢复操作。')
    journal_path().unlink()
    shutil.rmtree(backup)
    if action == 'restore':
        for value in sorted(set(record['new_dirs']), key=lambda value: len(Path(value).parts), reverse=True):
            path = Path(value)
            if path == Path('/') or path == Path.home():
                raise ValueError('事务目录超出允许范围。')
            try:
                path.rmdir()
            except OSError:
                pass


def locked(arguments):
    state = user_paths()['state']
    path = lock_path()
    fd = os.open(path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    info = os.fstat(fd)
    if info.st_uid != os.getuid() or not stat.S_ISREG(info.st_mode):
        raise ValueError('维护锁所有者或类型无效。')
    fcntl.flock(fd, fcntl.LOCK_EX)
    # Keep the descriptor open in this supervisor until every child exits.
    result = subprocess.run(arguments, env=os.environ | {'POINTERACCEL_MAINTENANCE_LOCKED': state})
    return result.returncode


def main(args):
    command, *args = args
    if command == 'elf-id':
        print(elf_id(args[0]))
    elif command == 'fingerprint':
        write(args[0], fingerprint())
    elif command == 'verify':
        verify(json.loads(Path(args[0]).read_text()))
    elif command == 'manifest':
        info, module, output, owner = args
        record = json.loads(Path(info).read_text())
        current = fingerprint()
        for key in current.keys() - {'packages'}:
            if record.get(key) != current[key]:
                raise ValueError(f'构建信息与系统不匹配: {key}')
        record.update(current)
        record.update(schema=1, owner=int(owner), module_sha256=digest(module), module_build_id=elf_id(module))
        write(output, record)
    elif command == 'validate':
        record = json.loads(Path(args[0]).read_text())
        if (
            record.get('schema') != 1
            or record.get('owner') != int(args[1])
            or record.get('kwin_iid') != 'org.kde.kwin.PluginFactoryInterface' + record.get('kwin_version', '')
            or not re.fullmatch('[a-f0-9]{64}', record.get('module_sha256', ''))
        ):
            raise ValueError('系统兼容清单无效。')
        verify(record)
        if len(args) > 2 and (
            digest(args[2]) != record['module_sha256'] or elf_id(args[2]) != record['module_build_id']
        ):
            raise ValueError('模块与兼容清单不匹配。')
    elif command == 'snapshot':
        snapshot(*args)
    elif command == 'extract':
        extract(*args)
    elif command == 'locked':
        return locked(args)
    elif command == 'journal-path':
        print(journal_path())
    elif command == 'journal-create':
        backup, system_owner, compatibility, *directories = args
        manifest = json.loads(Path(compatibility).read_text())
        before = Path(system_owner).read_text() if Path(system_owner).exists() else ''
        after = f'{os.getuid()}\t{manifest["module_sha256"]}\t{digest(compatibility)}\n'
        with open(journal_path(), 'x') as stream:
            os.chmod(journal_path(), 0o600)
            json.dump(
                {
                    'backup': backup,
                    'paths': user_paths(),
                    'new_dirs': directories,
                    'system_before': before,
                    'system_after': after,
                },
                stream,
            )
            stream.flush()
            os.fsync(stream.fileno())
    elif command == 'journal-action':
        record = load_journal()
        current = Path(args[0]).read_text() if Path(args[0]).exists() else ''
        if current == record['system_before']:
            print('restore')
        elif current == record['system_after']:
            print('keep')
        else:
            raise ValueError('系统记录与事务前后状态都不匹配；已保留备份，需要人工核查。')
    elif command == 'recover-user':
        recover_user(args[0])
    elif command == 'unit':
        output, executable, config, data, state = args

        def quote(text, command=False):
            text = text.replace('\\', '\\\\').replace('"', '\\"').replace('%', '%%')
            return '"' + (text.replace('$', '$$') if command else text) + '"'

        Path(output).write_text(
            '[Unit]\nDescription=PointerAccel compatibility check and session loader\n'
            'After=plasma-kwin_wayland.service\nPartOf=graphical-session.target\n'
            '[Service]\nType=simple\nExecStart='
            + quote(executable, command=True)
            + ' internal-watch\n'
            + '\n'.join(
                'Environment=' + quote(key + '=' + value)
                for key, value in [('XDG_CONFIG_HOME', config), ('XDG_DATA_HOME', data), ('XDG_STATE_HOME', state)]
            )
            + '\nRestart=on-failure\nRestartSec=10\n[Install]\nWantedBy=plasma-workspace.target\n'
        )
    else:
        raise ValueError(f'未知维护操作: {command}')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv[1:]))
    except (ValueError, OSError, subprocess.SubprocessError, tarfile.TarError) as error:
        print(f'PointerAccel: {error}', file=sys.stderr)
        sys.exit(1)
