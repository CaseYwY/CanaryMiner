#!/usr/bin/env python3
"""Inspect both fixed release artifacts; never execute them, build, delete, archive or publish anything."""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import stat
import subprocess
import sys

LINUX_SYSTEM = frozenset(('libc.so.6', 'libm.so.6', 'libdl.so.2', 'libpthread.so.0', 'librt.so.1',
                          'libstdc++.so.6', 'libgcc_s.so.1', 'ld-linux-x86-64.so.2'))
WINDOWS_SYSTEM = frozenset(('kernel32.dll', 'user32.dll', 'advapi32.dll', 'shell32.dll', 'ole32.dll',
                            'ws2_32.dll', 'bcrypt.dll', 'crypt32.dll', 'secur32.dll', 'ntdll.dll',
                            'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll',
                            'vcruntime140_1.dll', 'ucrtbase.dll'))
MAX_ARTIFACT_BYTES = 256 * 1024 * 1024
HOME_PATH = re.compile(r'(?i)[a-z]:[\\/]+Users[\\/]+|/(?:home|Users)/|/mnt/[a-z]/')


def read_artifact(path, platform):
    mode = path.lstat()
    if not stat.S_ISREG(mode.st_mode) or not 64 <= mode.st_size <= MAX_ARTIFACT_BYTES:
        raise ValueError(path.name + ': expected bounded nonempty regular artifact, not a link')
    data = path.read_bytes()
    if len(data) != mode.st_size:
        raise ValueError(path.name + ': artifact changed while reading')
    if platform == 'linux':
        valid = data[:6] == b'\x7fELF\x02\x01' and data[18:20] == b'\x3e\x00'
    else:
        offset = int.from_bytes(data[60:64], 'little')
        valid = data[:2] == b'MZ' and 64 <= offset <= len(data) - 26 and data[offset:offset + 6] == b'PE\0\0\x64\x86' and data[offset + 24:offset + 26] == b'\x0b\x02'
    if not valid:
        raise ValueError(path.name + ': not a supported x86-64 native image')
    return data


def check_cache(path):
    lines = path.read_text(encoding='utf8').splitlines()
    for key in ('CANARY_ENABLE_ENERGY_RUNTIME', 'CANARY_ENABLE_WORKER_CONTROL_RUNTIME', 'CANARY_FEE_TEST_POLICY'):
        if [line for line in lines if line.startswith(key + ':')] != [key + ':BOOL=OFF']:
            raise ValueError('release cache must explicitly disable development runtime and test fee policy')
    if any(line.startswith('CANARY_ENABLE_') and not re.fullmatch(r'CANARY_ENABLE_[A-Z0-9_]+:BOOL=OFF', line) for line in lines):
        raise ValueError('optional development capabilities are not release-qualified')


def privacy_check(data, name, checks, local, public_fee):
    # Scan complete decoded views, not only six-byte ASCII runs. Windows stores paths/literals as UTF-16 too.
    def views():
        yield data.decode('utf8', errors='replace')
        for encoding in ('utf-16-le', 'utf-16-be'):
            for offset in (0, 1):
                yield data[offset:].decode(encoding, errors='ignore')
    for text in views():
        if HOME_PATH.search(text):
            raise ValueError(name + ': build-machine path category detected (private bytes withheld)')
        # Only the exact declared public fee destination is exempt; a matching prefix is not an exemption.
        text = re.sub(r'(?<![A-Za-z0-9_])' + re.escape(public_fee) + r'(?![A-Za-z0-9_])', '<public-donation-address>', text)
        findings = []
        checks.scan_text(name, text.encode('utf8'), local, findings)
        if findings:
            raise ValueError(name + ': private or credential category detected (private bytes withheld)')


def inspect(tool, arguments):
    try:
        result = subprocess.run([str(tool), *arguments], check=True, capture_output=True, timeout=30,
                                encoding='utf8', errors='replace', env=dict(os.environ, LC_ALL='C', VSLANG='1033'))
    except (OSError, subprocess.SubprocessError) as from_error:
        raise ValueError('native metadata inspector failed; details withheld') from from_error
    return result.stdout


def dependency_check(platform, name, metadata, symbols=''):
    helper = name.startswith('canary_preferences_worker')
    if platform == 'linux':
        needed = [value.strip() for value in re.findall(r'^\s*NEEDED[ \t]+([^\r\n]+)$', metadata, re.M)]
        allowed = LINUX_SYSTEM | (frozenset() if helper else frozenset(('libcuda.so.1',)))
        if not needed or any(item not in allowed for item in needed):
            raise ValueError(name + ': unsupported or unreadable native dependencies')
        versions = [tuple(map(int, value.split('.'))) for value in re.findall(r'\bGLIBC_([0-9]+(?:\.[0-9]+)+)\b', symbols)]
        if not versions or not re.search(r'\*UND\*[^\n]*\bpidfd_spawn\s*$', symbols, re.M):
            raise ValueError(name + ': creation-bound pidfd capability was not built into the image')
        return max([(2, 40), *versions])
    needed, inside, started = [], False, False
    for line in metadata.splitlines():
        if re.fullmatch(r'\s*Image has the following (?:delay load )?dependencies:\s*', line, re.I):
            inside, started = True, False
        elif inside:
            if line.strip():
                needed.append(line.strip().lower())
                started = True
            elif started:
                inside = False
    def allowed(value):
        return value in WINDOWS_SYSTEM or bool(re.fullmatch(r'api-ms-win-(?:crt|core)-[a-z0-9-]+\.dll', value)) or (not helper and value == 'nvcuda.dll')
    if not needed or any(not allowed(item) for item in needed):
        raise ValueError(name + ': unsupported or unreadable native dependencies')
    return ()


def verify(platform, stage, source, cache, deny_file, tool):
    check_cache(cache)
    spec = importlib.util.spec_from_file_location('release_secret_checks', source / 'tools/check_secrets.py')
    checks = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(checks)
    # Explicit strict read closes its handle, never changes the environment and never exposes the private literals.
    with deny_file.open(encoding='utf8') as handle:
        local = [line.rstrip('\r\n') for line in handle if line.rstrip('\r\n') and not line.lstrip().startswith('#')]
    if not local:
        raise ValueError('explicit private deny-list must not be empty')
    fee_source = (source / 'src/product/fee.h').read_text(encoding='utf8')
    match = re.search(r'^\s*#define CANARY_FEE_DESTINATION "(prl1[a-z0-9]+)"\s*$', fee_source, re.M)
    if not match:
        raise ValueError('declared public fee metadata unavailable')
    names = ('canary-miner', 'canary_preferences_worker')
    if platform == 'windows':
        names = tuple(name + '.exe' for name in names)
    images = [(stage / name, read_artifact(stage / name, platform)) for name in names]
    floor = (2, 40) if platform == 'linux' else ()
    for path, data in images:
        privacy_check(data, path.name, checks, local, match.group(1))
        if platform == 'linux':
            metadata, symbols = inspect(tool, ['-p', str(path)]), inspect(tool, ['-T', str(path)])
        else:
            metadata, symbols = inspect(tool, ['/DEPENDENTS', str(path)]), ''
        floor = max(floor, dependency_check(platform, path.name, metadata, symbols))
    for path, data in images:
        if read_artifact(path, platform) != data:
            raise ValueError(path.name + ': artifact changed during metadata inspection')
    return '.'.join(map(str, floor))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=('linux', 'windows'), required=True)
    for name in ('stage', 'source', 'cache', 'deny-file', 'tool'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    try:
        floor = verify(args.platform, args.stage, args.source, args.cache, args.deny_file, args.tool)
    except (OSError, ValueError):
        print('verify_artifacts: REFUSED; missing/changed artifact, private content, capability or dependency checks failed. Private details withheld.', file=sys.stderr)
        return 1
    print(floor or 'windows-artifacts-checked')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
