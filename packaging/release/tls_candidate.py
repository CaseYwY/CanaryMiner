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
import zlib

from verify_artifacts import ENERGY_RUNTIME_MARKER, MAX_ARTIFACT_BYTES, PUBLIC_JSON_WORKER, energy_runtime_present, verify_images


SCHEMA = 'canaryminer-tls-candidate-v1'
NAMED_TZDB_SCHEMA = 'canaryminer-tls-named-tzdb-candidate-v2'
PUBLIC_HTTPS_SCHEMA = 'canaryminer-tls-named-tzdb-public-https-candidate-v3'
ENERGY_RUNTIME_SCHEMA = 'canaryminer-tls-energy-runtime-candidate-v4'
BILLING_WATCH_SCHEMA = 'canaryminer-tls-billing-watch-candidate-v5'
OCTOPUS_ACCOUNT_SCHEMA = 'canaryminer-tls-octopus-account-candidate-v6'
BILLING_WATCH_FILES = {'linux': 'billing-watch.sh', 'windows': 'Register-BillingWatch.ps1'}
BILLING_WATCH_QUIET = b'--watch-quiet\0'
OCTOPUS_ACCOUNT_FILES = ('tools/capture_octopus_consumption.py', 'tools/prepare_octopus_consumption.py',
                         'tools/capture_octopus_rates.py')
ENERGY_WORKERS = ('canary_energy_authorization_worker', 'canary_energy_collector_worker')
ENERGY_ROLE_MARKERS = (b'CanaryMiner-EnergyAuthorizationWorker/1\0', b'CanaryMiner-EnergyCollectorWorker/1\0')
ENERGY_SQLITE = {'windows': 'winsqlite3.dll', 'linux': 'libsqlite3.so.0'}
ENERGY_FIXTURE_MARKERS = (b'fixture-stall-before.json', b'fixture-stall-after.json', b'fixture-bad-ack.json',
                          b'fixture-lease-no-ack.json', b'fixture-guard-delay.json',
                          b'fixture-parent-death.json', b'fixture-entropy-fail.json', b'.fixture-ready')
PUBLIC_HTTPS_OPT_IN = b'  public-https=opt-in\0'
PUBLIC_HTTPS_AGENT = b'CanaryMiner-PublicData/1\0'
PUBLIC_DOH_SELECTOR = b'cloudflare-doh\0'
CURL_VERSION = '8.23.0'
CARES_VERSION = '1.34.9'
LINUX_OPENSSL_VERSION = b'OpenSSL 3.5.9 '
PUBLIC_HTTPS_NOTICE_SHA256 = {
    'LICENSES/curl-8.23.0.txt': '82f2f4427d6545ee5aaac4f0b80428da6cc8ba41c2cf5da3a03680ec327b9681',
    'LICENSES/c-ares-1.34.9.txt': '460f5e768fda3752ca2169a95df062578a10fb126bfd65f3b9b1a1bed2f84807',
}
TZDB_DIR = 'canary-tzdb-2026e'
TZDB_FILES = ('africa', 'antarctica', 'asia', 'australasia', 'backward', 'etcetera',
              'europe', 'northamerica', 'southamerica', 'leapseconds', 'version')
# SHA-256 of the sorted 'filename SHA-256\n' inventory from tariff_tzdb_files.inc.
# An aggregate pin lets the standalone installer authenticate these files offline.
TZDB_INVENTORY_SHA256 = {
    'linux': 'd5cd786140b5170a4788f55fec6e908c53242b72ee73a2f2295f9ab34af08b3d',
    'windows': '088b7d0d940bc512b89f328b44ddb78a59c042fdea8e9996b18cf58432609656',
}
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
                'CANARY_ENABLE_POOL_TLS_PRIVATE_TRUST',
                'CANARY_FEE_TEST_POLICY')
TLS_RUNTIME = 'CANARY_ENABLE_POOL_TLS_RUNTIME'
PUBLIC_HTTPS = 'CANARY_ENABLE_PUBLIC_HTTPS'
ENERGY_RUNTIME = 'CANARY_ENABLE_ENERGY_RUNTIME'
NAMED_TZDB = 'CANARY_ENABLE_NAMED_TZDB'
BUNDLED_TZDB = 'CANARY_BUNDLED_TZDB_DIR'


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


def images(platform, public_https=False, energy_runtime=False):
    names = IMAGE_NAMES
    if public_https:
        names += (PUBLIC_JSON_WORKER,)
    if energy_runtime:
        names += ENERGY_WORKERS
    return tuple(name + ('.exe' if platform == 'windows' else '') for name in names)


def tzdb_paths(platform):
    names = (*TZDB_FILES, 'windowsZones.xml') if platform == 'windows' else TZDB_FILES
    return {name: f'bin/{TZDB_DIR}/{name}' for name in names}


def billing_watch_path(platform):
    return f'deploy/{BILLING_WATCH_FILES[platform]}'


def file_limits(platform, named_tzdb=False, public_https=False, energy_runtime=False, billing_watch=False,
                octopus_account=False):
    return {**{f'bin/{name}': MAX_ARTIFACT_BYTES for name in images(platform, public_https, energy_runtime)},
            **dict.fromkeys(RELEASE_FILES, MAX_TEXT_BYTES),
            **dict.fromkeys(tzdb_paths(platform).values() if named_tzdb else (), MAX_TEXT_BYTES),
            **({billing_watch_path(platform): MAX_TEXT_BYTES} if billing_watch or octopus_account else {}),
            **dict.fromkeys(OCTOPUS_ACCOUNT_FILES if octopus_account else (), MAX_TEXT_BYTES),
            **({OPENSSL_LICENSE: MAX_TEXT_BYTES} if platform == 'linux' else {}),
            MANIFEST: MAX_TEXT_BYTES}


def file_mode(name):
    if name == billing_watch_path('linux') or (name.startswith('bin/') and name.count('/') == 1):
        return 0o755
    return 0o644


def check_cache(path, platform, named_tzdb=False, public_https=False, energy_runtime=False):
    if public_https and not named_tzdb:
        raise ValueError('public HTTPS candidate also requires the bundled tariff TZDB')
    keys = {}
    for line in read_regular(path, MAX_TEXT_BYTES).decode('utf8').splitlines():
        if line.startswith(('#', '//')) or '=' not in line or ':' not in line.partition('=')[0]:
            continue
        field, _, value = line.partition('=')
        key, _, kind = field.partition(':')
        if key.startswith('CANARY_ENABLE_') or key in ('CANARY_PROFILE', 'CMAKE_BUILD_TYPE',
                                                       'CANARY_FEE_TEST_POLICY', '_OPENSSL_VERSION',
                                                       BUNDLED_TZDB):
            if key in keys:
                raise ValueError('candidate cache has duplicate capability or profile')
            keys[key] = (kind, value)
    if (keys.get('CANARY_PROFILE') != ('STRING', 'production-sm120') or
            keys.get('CMAKE_BUILD_TYPE') != ('STRING', 'Release') or
            keys.get(TLS_RUNTIME) != ('BOOL', 'ON') or
            keys.get(NAMED_TZDB) != ('BOOL', 'ON' if named_tzdb else 'OFF') or
            keys.get(PUBLIC_HTTPS) != ('BOOL', 'ON' if public_https else 'OFF') or
            any(keys.get(key) != ('BOOL', 'ON' if key == ENERGY_RUNTIME and energy_runtime else 'OFF')
                for key in REQUIRED_OFF)):
        raise ValueError('candidate requires Release SM120 and the selected capabilities')
    enabled = {TLS_RUNTIME}
    if named_tzdb:
        enabled.add(NAMED_TZDB)
    if public_https:
        enabled.add(PUBLIC_HTTPS)
    if energy_runtime:
        enabled.add(ENERGY_RUNTIME)
    if any(value != ('BOOL', 'OFF') for key, value in keys.items()
           if key.startswith('CANARY_ENABLE_') and key not in enabled):
        raise ValueError('candidate includes an unsupported development capability')
    kind, bundled = keys.get(BUNDLED_TZDB, ('PATH', ''))
    if kind != 'PATH' or (not Path(bundled).is_absolute() if named_tzdb else bool(bundled)):
        raise ValueError('bundled tariff TZDB must match the selected candidate')
    if platform == 'linux' and keys.get('_OPENSSL_VERSION') != ('INTERNAL', '3.5.9'):
        raise ValueError('Linux candidate requires the inventoried OpenSSL 3.5.9 build')


def check_tzdb(platform, files):
    paths = tzdb_paths(platform)
    inventory = ''.join(f'{name} {sha256(files[paths[name]])}\n' for name in sorted(paths))
    if sha256(inventory.encode('ascii')) != TZDB_INVENTORY_SHA256[platform]:
        raise ValueError('tariff TZDB does not match the pinned file inventory')


def check_public_https(platform, files, energy_runtime=False):
    native = images(platform, public_https=True, energy_runtime=energy_runtime)
    miner, public_worker = native[0], PUBLIC_JSON_WORKER + ('.exe' if platform == 'windows' else '')
    if PUBLIC_HTTPS_OPT_IN not in files[f'bin/{miner}']:
        raise ValueError('the candidate miner lacks public HTTPS admission')
    if any(PUBLIC_HTTPS_AGENT in files[f'bin/{image}'] for image in native if image != public_worker):
        raise ValueError('public HTTPS transport must be confined to the public JSON worker')
    data = files[f'bin/{public_worker}']
    if (PUBLIC_DOH_SELECTOR in files[f'bin/{miner}']) != (PUBLIC_DOH_SELECTOR in data):
        raise ValueError('the candidate miner and public worker disagree on encrypted DNS')
    # The generic backend name also appears in the runtime's backend selection check.
    # Require strings from the selected libcurl implementation itself.
    backend = ((b'schannel: AcquireCredentialsHandle failed:',) if platform == 'windows' else
               (b'OpenSSL SSL_connect:', LINUX_OPENSSL_VERSION))
    versions = (CURL_VERSION.encode('ascii') + b'\0', CARES_VERSION.encode('ascii') + b'\0')
    required = (PUBLIC_HTTPS_AGENT, b'CMJSON1 ', b'libcurl\0', *backend, *versions)
    excluded = (b'fixture.test.invalid', b'MultiSSL\0')
    if not all(marker in data for marker in required) or any(marker in data for marker in excluded):
        raise ValueError('the candidate requires the inventoried production public HTTPS worker')
    for notice, expected in PUBLIC_HTTPS_NOTICE_SHA256.items():
        if sha256(files[notice].replace(b'\r\n', b'\n')) != expected:
            raise ValueError('public HTTPS notice does not match the inventoried dependency')


def check_energy_runtime(platform, files):
    miner = files[f'bin/{images(platform)[0]}']
    names = images(platform, energy_runtime=True)[-2:]
    authorization, collector = (files[f'bin/{name}'] for name in names)
    if (ENERGY_RUNTIME_MARKER not in miner or
            any(ENERGY_RUNTIME_MARKER in worker for worker in (authorization, collector))):
        raise ValueError('energy runtime requires a capable miner and two native workers')
    authorization_role, collector_role = ENERGY_ROLE_MARKERS
    if (authorization_role not in authorization or collector_role in authorization or
            collector_role not in collector or authorization_role in collector):
        raise ValueError('energy worker entry point identity mismatch')
    sqlite = ENERGY_SQLITE[platform].encode('utf-16-le' if platform == 'windows' else 'ascii')
    if (b'["CMEPQ1",' not in authorization or b'CMERQ1\n\0' not in collector or
            b'sqlite3_libversion_number\0' not in collector or sqlite not in collector):
        raise ValueError('energy runtime worker or system SQLite support is missing')
    for image in (miner, authorization, collector):
        if any(marker in image or marker.decode('ascii').encode('utf-16-le') in image
               for marker in ENERGY_FIXTURE_MARKERS):
            raise ValueError('energy candidate includes test-only worker hooks')


def manifest(platform, revision, glibc, files, named_tzdb=False, public_https=False, energy_runtime=False,
             billing_watch=False, octopus_account=False):
    backend = 'schannel' if platform == 'windows' else 'openssl-3.5.9-static'
    document = {
        'schema': NAMED_TZDB_SCHEMA if named_tzdb else SCHEMA,
        'platform': platform,
        'architecture': 'x86_64',
        'source_revision': revision,
        'build_profile': 'production-sm120',
        'tls_backend': backend,
        'min_glibc': glibc,
        'files': {name: {'bytes': len(data), 'sha256': sha256(data)}
                  for name, data in sorted(files.items())},
    }
    if public_https:
        document['schema'] = PUBLIC_HTTPS_SCHEMA
        document['public_https'] = {
            'tls_backend': backend,
            'curl': CURL_VERSION,
            'resolver': 'c-ares-' + CARES_VERSION,
        }
    if energy_runtime:
        document['schema'] = ENERGY_RUNTIME_SCHEMA
        document['named_tzdb'] = named_tzdb
        document['energy_runtime'] = {
            'default_consent': 'off',
            'sqlite_library': ENERGY_SQLITE[platform],
            'min_sqlite': '3.31.0',
        }
    if billing_watch or octopus_account:
        document['schema'] = OCTOPUS_ACCOUNT_SCHEMA if octopus_account else BILLING_WATCH_SCHEMA
        document['named_tzdb'] = named_tzdb
    return document


def valid_revision(revision):
    return isinstance(revision, str) and re.fullmatch('[0-9a-f]{40}', revision) is not None


def validate(platform, files):
    if MANIFEST not in files:
        raise ValueError('candidate manifest is missing')
    document = json.loads(files[MANIFEST], object_pairs_hook=one)
    if not isinstance(document, dict) or document.get('schema') not in (
            SCHEMA, NAMED_TZDB_SCHEMA, PUBLIC_HTTPS_SCHEMA, ENERGY_RUNTIME_SCHEMA,
            BILLING_WATCH_SCHEMA, OCTOPUS_ACCOUNT_SCHEMA):
        raise ValueError('invalid candidate manifest')
    schema = document['schema']
    octopus_account = schema == OCTOPUS_ACCOUNT_SCHEMA
    billing_watch = schema == BILLING_WATCH_SCHEMA or octopus_account
    energy_runtime = schema == ENERGY_RUNTIME_SCHEMA or (billing_watch and 'energy_runtime' in document)
    public_https = schema == PUBLIC_HTTPS_SCHEMA or ((energy_runtime or billing_watch) and 'public_https' in document)
    named_tzdb = (document.get('named_tzdb') if energy_runtime or billing_watch
                  else public_https or schema == NAMED_TZDB_SCHEMA)
    if type(named_tzdb) is not bool or ((public_https or billing_watch) and not named_tzdb):
        raise ValueError('invalid candidate clock capability')
    if set(files) != set(file_limits(platform, named_tzdb, public_https, energy_runtime, billing_watch, octopus_account)):
        raise ValueError('candidate inventory mismatch')
    miner, preferences, stream_worker = images(platform)
    if billing_watch and BILLING_WATCH_QUIET not in files[f'bin/{miner}']:
        raise ValueError('billing watcher requires a miner with quiet watch support')
    if named_tzdb or energy_runtime:
        if stream_worker.encode('ascii') + b'\0' not in files[f'bin/{miner}']:
            raise ValueError('pool TLS worker reference missing from the miner')
    if named_tzdb:
        check_tzdb(platform, files)
        # The miner and its preferences worker must both recognize the bundled clocks.
        pins = [sha256(files[path]).encode('ascii') for path in tzdb_paths(platform).values()]
        for image in (miner, preferences):
            data = files[f'bin/{image}']
            if any(pin not in data for pin in pins):
                raise ValueError('tariff TZDB pins missing from a native consumer')
    if public_https:
        check_public_https(platform, files, energy_runtime)
    elif PUBLIC_HTTPS_OPT_IN in files[f'bin/{images(platform)[0]}']:
        raise ValueError('public HTTPS miner requires the public HTTPS candidate profile')
    if energy_runtime:
        check_energy_runtime(platform, files)
    elif energy_runtime_present(files[f'bin/{miner}']):
        raise ValueError('energy-enabled miner requires the energy candidate profile')
    revision, glibc = document.get('source_revision'), document.get('min_glibc')
    valid_glibc = (glibc is None if platform == 'windows' else
                   isinstance(glibc, str) and re.fullmatch(r'[0-9]{1,3}(?:\.[0-9]{1,3}){1,2}', glibc)
                   and tuple(map(int, glibc.split('.'))) >= (2, 40))
    if not valid_revision(revision) or not valid_glibc:
        raise ValueError('invalid candidate revision or runtime requirement')
    actual = manifest(platform, revision, glibc, {k: v for k, v in files.items() if k != MANIFEST},
                      named_tzdb, public_https, energy_runtime, billing_watch, octopus_account)
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
    files, limits = {}, file_limits(platform, named_tzdb=True, public_https=True, energy_runtime=True,
                                   billing_watch=True, octopus_account=True)
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
        with gzip.GzipFile(filename='', fileobj=stream, mode='wb', mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode='w', format=tarfile.USTAR_FORMAT) as package:
                for name, data in sorted(files.items()):
                    entry = tarfile.TarInfo(name)
                    entry.size, entry.mode = len(data), file_mode(name)
                    package.addfile(entry, io.BytesIO(data))


def pack(platform, build, revision, deny_file, tool, archive, openssl_license, named_tzdb=False, public_https=False,
         energy_runtime=False, billing_watch=False, octopus_account=False):
    source_tree = Path(__file__).resolve().parent.parent.parent
    if not valid_revision(revision) or not archive.name.endswith(suffix(platform)):
        raise ValueError('invalid revision or candidate archive extension')
    if archive.exists() or archive.is_symlink():
        raise ValueError('candidate archive already exists')
    if (billing_watch or octopus_account) and not named_tzdb:
        raise ValueError('billing watch and account capture require the bundled tariff TZDB')
    check_cache(build / 'CMakeCache.txt', platform, named_tzdb, public_https, energy_runtime)
    files = {name: read_regular(source_tree / source, MAX_TEXT_BYTES)
             for name, source in RELEASE_FILES.items()}
    if billing_watch or octopus_account:
        files[billing_watch_path(platform)] = read_regular(
            source_tree / 'packaging' / 'release' / BILLING_WATCH_FILES[platform], MAX_TEXT_BYTES)
    if octopus_account:
        files.update({name: read_regular(source_tree / name, MAX_TEXT_BYTES) for name in OCTOPUS_ACCOUNT_FILES})
    tzdb = {}
    if named_tzdb:
        tzdb = {destination: read_regular(build / TZDB_DIR / name, MAX_TEXT_BYTES)
                for name, destination in tzdb_paths(platform).items()}
        # These independently pinned public source bytes can include public URLs with /home/.
        check_tzdb(platform, tzdb)
    if platform == 'linux':
        if openssl_license is None:
            raise ValueError('the OpenSSL 3.5.9 license is required')
        files[OPENSSL_LICENSE] = read_regular(openssl_license, MAX_TEXT_BYTES)
    elif openssl_license is not None:
        raise ValueError('Windows uses Schannel')
    floor, checked = verify_images(platform, build, source_tree, deny_file, tool, images(platform, public_https, energy_runtime),
                                   portable=True, payloads=files)
    if platform == 'linux' and LINUX_OPENSSL_VERSION not in checked[IMAGE_NAMES[2]]:
        raise ValueError('the static OpenSSL 3.5.9 image is required')
    files.update(tzdb)
    files.update({f'bin/{name}': data for name, data in checked.items()})
    glibc = '.'.join(map(str, floor)) if floor else None
    document = manifest(platform, revision, glibc, files, named_tzdb, public_https, energy_runtime,
                        billing_watch, octopus_account)
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
            command.add_argument('--with-named-tzdb', action='store_true',
                                 help='include the reviewed IANA 2026e data for named tariff clocks')
            command.add_argument('--with-public-https', action='store_true',
                                 help='include the opt-in public JSON worker (requires --with-named-tzdb)')
            command.add_argument('--with-energy-runtime', action='store_true',
                                 help='include both workers for opt-in energy persistence and journal review')
            command.add_argument('--with-billing-watch', action='store_true',
                                 help='include host deployment tooling (requires --with-named-tzdb)')
            command.add_argument('--with-octopus-account', action='store_true',
                                 help='include interactive Octopus capture and watch deployment (requires --with-named-tzdb)')
        else:
            command.add_argument('--sha256', required=True)
        if action == 'install':
            command.add_argument('--prefix', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.operation == 'pack':
            print(pack(args.platform, args.build, args.revision, args.deny_file, args.tool,
                       args.archive, args.openssl_license, args.with_named_tzdb, args.with_public_https,
                       args.with_energy_runtime, args.with_billing_watch, args.with_octopus_account))
        else:
            if not re.fullmatch('[0-9a-f]{64}', args.sha256):
                raise ValueError('expected a trusted SHA-256 digest')
            if args.operation == 'install':
                document, digest = install(args.platform, args.archive, args.sha256, args.prefix)
            else:
                _, document, digest = load(args.platform, args.archive, args.sha256)
            print(json.dumps({'sha256': digest, **document}, sort_keys=True))
    except (OSError, ValueError, EOFError, RuntimeError, tarfile.TarError, zipfile.BadZipFile,
            zlib.error) as error:
        # Exception text may contain a build-machine path or untrusted archive member name.
        print(f'tls_candidate: {args.operation} refused ({type(error).__name__}).', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
