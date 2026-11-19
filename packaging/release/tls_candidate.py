#!/usr/bin/env python3
"""Package, check or install an opt-in TLS candidate without running its binaries."""

import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import re
import stat
import sys
import tarfile
import tempfile
import zipfile

from verify_artifacts import MAX_ARTIFACT_BYTES, verify_images


SCHEMA = 'canaryminer-tls-candidate-v1'
MANIFEST = 'manifest.json'
IMAGE_NAMES = ('canary-miner', 'canary_preferences_worker', 'canary_pool_tls_stream_worker')
RELEASE_FILES = {
    'README.txt': 'packaging/release/README-tls-candidate.txt',
    'config.tls.example.json': 'config/config.tls.example.json',
    'LICENSE': 'LICENSE',
    'THIRD_PARTY.md': 'THIRD_PARTY.md',
}
LICENSE_FILES = (
    'MIT-UPSTREAM.txt', 'Pearl-821c811d.txt', 'CLDR-7afa5cdb.txt',
    'IANA-tzdata-2026e.txt', 'date-3.0.5-notices.txt', 'ICU-79.1.txt',
    'curl-8.23.0.txt', 'c-ares-1.34.9.txt',
    *(f'NVIDIA-CUDA-13.1.1/{name}' for name in (
        'provenance.json', 'libnvvm.txt', 'cuda_cccl.txt', 'cuda_crt.txt',
        'cuda_cudart.txt', 'cuda_cuobjdump.txt', 'cuda_nvcc.txt',
    )),
)
RELEASE_FILES.update({f'LICENSES/{name}': f'LICENSES/{name}' for name in LICENSE_FILES})
OPENSSL_LICENSE = 'LICENSES/OpenSSL-3.5.9.txt'
OPENSSL_LICENSE_SHA256 = '7d5450cb2d142651b8afa315b5f238efc805dad827d91ba367d8516bc9d49e7a'
MAX_TEXT_BYTES = 256 * 1024
MAX_PACKAGE_BYTES = 96 * 1024 * 1024
REQUIRED_OFF = ('CANARY_ENABLE_ENERGY_RUNTIME', 'CANARY_ENABLE_WORKER_CONTROL_RUNTIME',
                'CANARY_ENABLE_POOL_TLS_PRIVATE_TRUST', 'CANARY_FEE_TEST_POLICY')
TLS_RUNTIME = 'CANARY_ENABLE_POOL_TLS_RUNTIME'


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def one(properties):
    result = {}
    for key, value in properties:
        if key in result:
            raise ValueError('duplicate manifest field')
        result[key] = value
    return result


def read_regular(path, limit):
    metadata = path.lstat()
    reparse = getattr(metadata, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT
    if reparse or not stat.S_ISREG(metadata.st_mode) or not 0 < metadata.st_size <= limit:
        raise ValueError('expected a nonempty bounded regular file')
    with path.open('rb') as stream:
        data = stream.read(metadata.st_size + 1)
    if len(data) != metadata.st_size:
        raise ValueError('file changed while reading')
    return data


def images(platform):
    return tuple(name + ('.exe' if platform == 'windows' else '') for name in IMAGE_NAMES)


def file_limits(platform):
    return {**{f'bin/{name}': MAX_ARTIFACT_BYTES for name in images(platform)},
            **dict.fromkeys(RELEASE_FILES, MAX_TEXT_BYTES),
            **({OPENSSL_LICENSE: MAX_TEXT_BYTES} if platform == 'linux' else {}),
            MANIFEST: MAX_TEXT_BYTES}


def file_mode(name):
    return 0o755 if name.startswith('bin/') else 0o644


def check_cache(path, platform):
    keys = {}
    for line in read_regular(path, MAX_TEXT_BYTES).decode('utf8').splitlines():
        if line.startswith(('#', '//')) or '=' not in line or ':' not in line.partition('=')[0]:
            continue
        field, _, value = line.partition('=')
        key, _, kind = field.partition(':')
        if key.startswith('CANARY_ENABLE_') or key in ('CANARY_PROFILE', 'CMAKE_BUILD_TYPE',
                                                       'CANARY_FEE_TEST_POLICY', '_OPENSSL_VERSION'):
            if key in keys:
                raise ValueError('candidate cache has duplicate capability or profile')
            keys[key] = (kind, value)
    if (keys.get('CANARY_PROFILE') != ('STRING', 'production-sm120') or
            keys.get('CMAKE_BUILD_TYPE') != ('STRING', 'Release') or
            keys.get(TLS_RUNTIME) != ('BOOL', 'ON') or
            any(keys.get(key) != ('BOOL', 'OFF') for key in REQUIRED_OFF)):
        raise ValueError('candidate requires Release SM120, TLS ON and explicit unrelated defaults OFF')
    if any(value != ('BOOL', 'OFF') for key, value in keys.items()
           if key.startswith('CANARY_ENABLE_') and key != TLS_RUNTIME):
        raise ValueError('candidate includes an unsupported development capability')
    if platform == 'linux' and keys.get('_OPENSSL_VERSION') != ('INTERNAL', '3.5.9'):
        raise ValueError('Linux candidate requires the inventoried OpenSSL 3.5.9 build')


def manifest(platform, revision, glibc, files):
    return {
        'schema': SCHEMA,
        'platform': platform,
        'architecture': 'x86_64',
        'source_revision': revision,
        'build_profile': 'production-sm120',
        'tls_backend': 'schannel' if platform == 'windows' else 'openssl-3.5.9-static',
        'min_glibc': glibc,
        'files': {name: {'bytes': len(data), 'sha256': sha256(data)}
                  for name, data in sorted(files.items())},
    }


def valid_revision(revision):
    return isinstance(revision, str) and re.fullmatch('[0-9a-f]{40}', revision) is not None


def validate(platform, files):
    if set(files) != set(file_limits(platform)):
        raise ValueError('candidate inventory mismatch')
    document = json.loads(files[MANIFEST], object_pairs_hook=one)
    if not isinstance(document, dict):
        raise ValueError('invalid candidate manifest')
    revision, glibc = document.get('source_revision'), document.get('min_glibc')
    valid_glibc = (glibc is None if platform == 'windows' else
                   isinstance(glibc, str) and re.fullmatch(r'[0-9]{1,3}(?:\.[0-9]{1,3}){1,2}', glibc)
                   and tuple(map(int, glibc.split('.'))) >= (2, 40))
    if not valid_revision(revision) or not valid_glibc:
        raise ValueError('invalid candidate revision or runtime requirement')
    actual = manifest(platform, revision, glibc, {k: v for k, v in files.items() if k != MANIFEST})
    if document != actual:
        raise ValueError('candidate manifest or file hash mismatch')
    if platform == 'linux' and sha256(files[OPENSSL_LICENSE]) != OPENSSL_LICENSE_SHA256:
        raise ValueError('OpenSSL license does not match the inventoried version')
    return document


def suffix(platform):
    return '.zip' if platform == 'windows' else '.tar.gz'


def load(platform, archive, expected=None):
    data = read_regular(archive, MAX_PACKAGE_BYTES)
    digest = sha256(data)
    if expected is not None and digest != expected:
        raise ValueError('candidate archive hash mismatch')
    files, limits = {}, file_limits(platform)
    total = 0

    def add(name, size, mode, open_entry):
        nonlocal total
        if name not in limits or name in files or not 0 < size <= limits[name] or mode != file_mode(name):
            raise ValueError('unexpected, linked, duplicate or invalid candidate member')
        total += size
        if total > MAX_PACKAGE_BYTES:
            raise ValueError('candidate expanded size exceeds the limit')
        with open_entry() as stream:
            contents = stream.read(size + 1)
        if len(contents) != size:
            raise ValueError('invalid candidate member length')
        files[name] = contents

    if platform == 'windows':
        with zipfile.ZipFile(io.BytesIO(data)) as package:
            for entry in package.infolist():
                mode = entry.external_attr >> 16
                if entry.orig_filename != entry.filename or not stat.S_ISREG(mode) or entry.flag_bits & 1:
                    raise ValueError('candidate ZIP members must be regular and unencrypted')
                add(entry.filename, entry.file_size, stat.S_IMODE(mode), lambda: package.open(entry))
    else:
        # Bound decompression before tarfile interprets extension records.
        tar_limit = MAX_PACKAGE_BYTES + 128 * 1024  # fixed file headers and USTAR padding
        with gzip.GzipFile(fileobj=io.BytesIO(data)) as stream:
            expanded = stream.read(tar_limit + 1)
        if len(expanded) > tar_limit:
            raise ValueError('candidate TAR expanded size exceeds the limit')
        with tarfile.open(fileobj=io.BytesIO(expanded), mode='r:') as package:
            for entry in package:
                if entry.type != tarfile.REGTYPE or entry.pax_headers or entry.sparse is not None:
                    raise ValueError('candidate TAR members must be plain regular files')
                add(entry.name, entry.size, entry.mode, lambda: package.extractfile(entry))
    return files, validate(platform, files), digest


def write_package(platform, files, stream):
    if platform == 'windows':
        with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_DEFLATED) as package:
            for name, data in sorted(files.items()):
                entry = zipfile.ZipInfo(name)
                entry.create_system = 3
                entry.external_attr = (stat.S_IFREG | file_mode(name)) << 16
                entry.compress_type = zipfile.ZIP_DEFLATED
                package.writestr(entry, data)
    else:
        with gzip.GzipFile(fileobj=stream, mode='wb', mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode='w', format=tarfile.USTAR_FORMAT) as package:
                for name, data in sorted(files.items()):
                    entry = tarfile.TarInfo(name)
                    entry.size, entry.mode = len(data), file_mode(name)
                    package.addfile(entry, io.BytesIO(data))


def pack(platform, build, revision, deny_file, tool, archive, openssl_license):
    source_tree = Path(__file__).resolve().parent.parent.parent
    if not valid_revision(revision) or not archive.name.endswith(suffix(platform)):
        raise ValueError('invalid revision or candidate archive extension')
    if archive.exists() or archive.is_symlink():
        raise ValueError('candidate archive already exists')
    check_cache(build / 'CMakeCache.txt', platform)
    files = {}
    for name, source in RELEASE_FILES.items():
        files[name] = read_regular(source_tree / source, MAX_TEXT_BYTES)
    if platform == 'linux':
        if openssl_license is None:
            raise ValueError('the OpenSSL 3.5.9 license is required')
        files[OPENSSL_LICENSE] = read_regular(openssl_license, MAX_TEXT_BYTES)
    elif openssl_license is not None:
        raise ValueError('Windows uses Schannel')
    floor, checked = verify_images(platform, build, source_tree, deny_file, tool, images(platform),
                                   portable=True, payloads=files)
    if platform == 'linux' and b'OpenSSL 3.5.9 ' not in checked[IMAGE_NAMES[2]]:
        raise ValueError('the static OpenSSL 3.5.9 image is required')
    files.update({f'bin/{name}': data for name, data in checked.items()})
    glibc = '.'.join(map(str, floor)) if floor else None
    document = manifest(platform, revision, glibc, files)
    files[MANIFEST] = (json.dumps(document, indent=2, sort_keys=True) + '\n').encode('utf8')
    validate(platform, files)
    if sum(map(len, files.values())) > MAX_PACKAGE_BYTES:
        raise ValueError('candidate expanded size exceeds the limit')
    # Publish only after reading the complete archive back. A same-volume link refuses existing paths.
    with tempfile.TemporaryDirectory(prefix='.canary-tls-', dir=archive.parent) as directory:
        temporary = Path(directory) / archive.name
        with temporary.open('xb') as stream:
            write_package(platform, files, stream)
        checked_files, _, digest = load(platform, temporary)
        if checked_files != files:
            raise ValueError('candidate round-trip altered file bytes')
        os.link(temporary, archive)
    return digest


def install(platform, archive, expected, prefix):
    files, document, digest = load(platform, archive, expected)
    if platform == 'linux' and os.name != 'posix':
        raise ValueError('Linux candidate installation requires POSIX executable modes')
    prefix.mkdir()
    for name, data in sorted(files.items()):
        target = prefix / name
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('xb') as stream:
            stream.write(data)
        if platform == 'linux':
            target.chmod(file_mode(name))
    for name, data in files.items():
        if read_regular(prefix / name, len(data)) != data:
            raise ValueError('installed candidate byte mismatch')
        if platform == 'linux' and stat.S_IMODE((prefix / name).stat().st_mode) != file_mode(name):
            raise ValueError('installed candidate mode mismatch')
    return document, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='operation', required=True)
    for action in ('pack', 'verify', 'install'):
        command = sub.add_parser(action)
        command.add_argument('--platform', choices=('linux', 'windows'), required=True)
        command.add_argument('--archive', type=Path, required=True)
        if action == 'pack':
            command.add_argument('--build', type=Path, required=True)
            command.add_argument('--revision', required=True)
            command.add_argument('--deny-file', type=Path, required=True)
            command.add_argument('--tool', type=Path, required=True)
            command.add_argument('--openssl-license', type=Path)
        else:
            command.add_argument('--sha256', required=True)
        if action == 'install':
            command.add_argument('--prefix', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.operation == 'pack':
            print(pack(args.platform, args.build, args.revision, args.deny_file, args.tool,
                       args.archive, args.openssl_license))
        else:
            if not re.fullmatch('[0-9a-f]{64}', args.sha256):
                raise ValueError('expected a trusted SHA-256 digest')
            if args.operation == 'install':
                document, digest = install(args.platform, args.archive, args.sha256, args.prefix)
            else:
                _, document, digest = load(args.platform, args.archive, args.sha256)
            print(json.dumps({'sha256': digest, **document}, sort_keys=True))
    except (OSError, ValueError, EOFError, RuntimeError, tarfile.TarError, zipfile.BadZipFile) as error:
        # Exception text may contain a build-machine path or untrusted archive member name.
        print(f'tls_candidate: {args.operation} refused ({type(error).__name__}).', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
