"""Release candidate boundaries for both archive formats. No binary is executed."""
import gzip
import hashlib
import importlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import warnings
import zipfile


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'packaging/release'))
try:
    CANDIDATE = importlib.import_module('tls_candidate')
    RELEASE = importlib.import_module('verify_artifacts')
finally:
    sys.path.pop(0)


REVISION = 'abcdef0123456789abcdef0123456789abcdef01'
OPENSSL = b'OpenSSL 3.5.9 license fixture\n'


class CandidateTests(unittest.TestCase):
    def test_installer_can_be_loaded_directly_from_a_clean_root(self):
        script = Path(CANDIDATE.__file__)
        module = {'__file__': '/qa/tls_candidate.py', '__name__': 'standalone_candidate'}
        with patch.dict(sys.modules, {'verify_artifacts': RELEASE}), \
                patch('pathlib.Path.resolve', return_value=PurePosixPath(module['__file__'])):
            exec(compile(script.read_bytes(), script.name, 'exec'), module)
        self.assertTrue(callable(module['install']))

    def test_cache_refuses_wider_policy_and_official_release_still_refuses_tls(self):
        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory) / 'CMakeCache.txt'
            base = ['CANARY_PROFILE:STRING=production-sm120', 'CMAKE_BUILD_TYPE:STRING=Release',
                    'CANARY_ENABLE_POOL_TLS_RUNTIME:BOOL=ON', '_OPENSSL_VERSION:INTERNAL=3.5.9',
                    'CANARY_ENABLE_NAMED_TZDB:BOOL=OFF', 'CANARY_ENABLE_PUBLIC_HTTPS:BOOL=OFF',
                    *(f'{key}:BOOL=OFF' for key in CANDIDATE.REQUIRED_OFF)]
            cache.write_text('\n'.join(base), encoding='utf8')
            CANDIDATE.check_cache(cache, 'linux')
            CANDIDATE.check_cache(cache, 'windows')
            with self.assertRaises(ValueError):
                RELEASE.check_cache(cache)
            invalid = (
                ['CANARY_ENABLE_UNKNOWN:BOOL=ON'],
                ['CANARY_ENABLE_UNKNOWN:STRING=OFF'],
                ['CANARY_ENABLE_POOL_TLS_RUNTIME:BOOL=ON'],
                ['CANARY_ENABLE_POOL_TLS_PRIVATE_TRUST:BOOL=ON'],
                ['CANARY_ENABLE_PUBLIC_HTTPS:BOOL=ON'],
                ['CANARY_FEE_TEST_POLICY:STRING=OFF'],
            )
            for extra in invalid:
                with self.subTest(extra=extra):
                    cache.write_text('\n'.join(base + extra), encoding='utf8')
                    with self.assertRaises(ValueError):
                        CANDIDATE.check_cache(cache, 'windows')
            for key in ('CANARY_PROFILE', 'CANARY_ENABLE_POOL_TLS_RUNTIME', 'CANARY_FEE_TEST_POLICY',
                        'CANARY_ENABLE_PUBLIC_HTTPS', 'CANARY_ENABLE_NAMED_TZDB', '_OPENSSL_VERSION'):
                cache.write_text('\n'.join(line for line in base if not line.startswith(key)), encoding='utf8')
                with self.assertRaises(ValueError):
                    CANDIDATE.check_cache(cache, 'linux')

    def test_named_tzdb_requires_explicit_bundled_offline_build(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cache = root / 'CMakeCache.txt'
            base = ['CANARY_PROFILE:STRING=production-sm120', 'CMAKE_BUILD_TYPE:STRING=Release',
                    'CANARY_ENABLE_POOL_TLS_RUNTIME:BOOL=ON', 'CANARY_ENABLE_NAMED_TZDB:BOOL=ON',
                    'CANARY_ENABLE_PUBLIC_HTTPS:BOOL=OFF',
                    *(f'{key}:BOOL=OFF' for key in CANDIDATE.REQUIRED_OFF)]
            bundle = f'CANARY_BUNDLED_TZDB_DIR:PATH={root.as_posix()}'
            cache.write_text('\n'.join([*base, bundle]), encoding='utf8')
            CANDIDATE.check_cache(cache, 'windows', named_tzdb=True)
            with self.assertRaises(ValueError):
                CANDIDATE.check_cache(cache, 'windows')
            for entries in (base, [*base, 'CANARY_BUNDLED_TZDB_DIR:PATH=relative'],
                            [*base, bundle, 'CANARY_ENABLE_PUBLIC_HTTPS:BOOL=ON'],
                            [*base, bundle, 'CANARY_BUNDLED_TZDB_DIR:PATH=another']):
                with self.subTest(entries=len(entries)):
                    cache.write_text('\n'.join(entries), encoding='utf8')
                    with self.assertRaises(ValueError):
                        CANDIDATE.check_cache(cache, 'windows', named_tzdb=True)

    def test_public_https_profile_and_worker_dependencies_are_explicit(self):
        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory) / 'CMakeCache.txt'
            base = ['CANARY_PROFILE:STRING=production-sm120', 'CMAKE_BUILD_TYPE:STRING=Release',
                    'CANARY_ENABLE_POOL_TLS_RUNTIME:BOOL=ON', 'CANARY_ENABLE_NAMED_TZDB:BOOL=ON',
                    'CANARY_ENABLE_PUBLIC_HTTPS:BOOL=ON', f'CANARY_BUNDLED_TZDB_DIR:PATH={cache.parent}',
                    '_OPENSSL_VERSION:INTERNAL=3.5.9', *(f'{key}:BOOL=OFF' for key in CANDIDATE.REQUIRED_OFF)]
            cache.write_text('\n'.join(base), encoding='utf8')
            for platform in ('linux', 'windows'):
                CANDIDATE.check_cache(cache, platform, named_tzdb=True, public_https=True)
                with self.assertRaises(ValueError):
                    CANDIDATE.check_cache(cache, platform, named_tzdb=True)
                with self.assertRaises(ValueError):
                    CANDIDATE.check_cache(cache, platform, public_https=True)
            with self.assertRaises(ValueError):
                RELEASE.check_cache(cache)

        windows_worker = 'canary_public_json_worker.exe'
        deps = 'Image has the following dependencies:\n\n KERNEL32.dll\n IPHLPAPI.DLL\n\n'
        self.assertEqual(RELEASE.dependency_check('windows', windows_worker, deps), ())
        for name in CANDIDATE.images('windows'):
            with self.assertRaises(ValueError):
                RELEASE.dependency_check('windows', name, deps)
        with self.assertRaises(ValueError):
            RELEASE.dependency_check('windows', windows_worker, deps.replace('IPHLPAPI.DLL', 'WLDAP32.DLL'))
        with self.assertRaises(ValueError):
            RELEASE.dependency_check('linux', 'canary_public_json_worker', ' NEEDED libssl.so.3\n',
                                     ' *UND* (GLIBC_2.39) pidfd_spawn\n')

    def test_standalone_tzdb_pins_match_native_runtime(self):
        pins = Path(__file__).resolve().parents[1] / 'src/product/tariff_tzdb_files.inc'
        rows = re.findall(r'TARIFF_TZDB_(FILE|WINDOWS)\("([A-Za-z.]+)", "([0-9a-f]{64})"\)',
                          pins.read_text(encoding='utf8'))
        for platform in ('linux', 'windows'):
            with self.subTest(platform=platform):
                expected = sorted((name, digest) for mode, name, digest in rows
                                  if mode == 'FILE' or platform == 'windows')
                self.assertEqual(set(CANDIDATE.tzdb_paths(platform)), {name for name, _ in expected})
                inventory = ''.join(f'{name} {digest}\n' for name, digest in expected)
                self.assertEqual(CANDIDATE.TZDB_INVENTORY_SHA256[platform],
                                 hashlib.sha256(inventory.encode('ascii')).hexdigest())

    def test_gpu_allowlist_is_for_the_miner_only(self):
        symbols = ' *UND* (GLIBC_2.39) pidfd_spawn\n'
        pe_imports = 'Image has the following dependencies:\n\n KERNEL32.dll\n\n'
        pe_imports += 'Image has the following delay load dependencies:\n\n NVCUDA.dll\n'
        self.assertEqual(RELEASE.dependency_check('linux', 'canary-miner', ' NEEDED libcuda.so.1\n', symbols), (2, 40))
        self.assertEqual(RELEASE.dependency_check('windows', 'canary-miner.exe', pe_imports), ())
        for linux, windows in zip(CANDIDATE.images('linux')[1:], CANDIDATE.images('windows')[1:]):
            with self.assertRaises(ValueError):
                RELEASE.dependency_check('linux', linux, ' NEEDED libcuda.so.1\n', symbols)
            with self.assertRaises(ValueError):
                RELEASE.dependency_check('windows', windows, pe_imports)

    def test_pack_checks_the_linux_system_interpreter(self):
        system_loader = b'/lib64/ld-linux-x86-64.so.2\0'

        def elf(*interpreters):
            header = bytearray(64)
            header[:6] = b'\x7fELF\x02\x01'
            struct.pack_into('<H', header, 18, 62)
            struct.pack_into('<Q', header, 32, len(header))
            struct.pack_into('<HH', header, 54, 56, len(interpreters) + 1)
            entries = bytearray(56)
            data = b''.join(interpreters)
            offset = len(header) + 56 * (len(interpreters) + 1)
            for interpreter in interpreters:
                entries.extend(struct.pack('<IIQQQQQQ', 3, 4, offset, 0, 0,
                                           len(interpreter), len(interpreter), 1))
                offset += len(interpreter)
            return bytes(header + entries) + data

        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            name = 'canary_pool_tls_stream_worker'
            image = stage / name
            source_and_policy = (stage, stage, stage, (name,))
            with patch.object(RELEASE, 'privacy_policy'), patch.object(RELEASE, 'privacy_check'), \
                    patch.object(RELEASE, 'inspect', side_effect=lambda _, args:
                                 ' NEEDED libc.so.6\n' if args[0] == '-p' else
                                 ' *UND* (GLIBC_2.39) pidfd_spawn\n'):
                image.write_bytes(elf(system_loader))
                _, data = RELEASE.verify_images('linux', stage, *source_and_policy, portable=True)
                self.assertEqual(data[name], image.read_bytes())
                rejected = (elf(b'/opt/loader/ld-linux-x86-64.so.2\0'), elf(), elf(system_loader)[:-1],
                            elf(system_loader, system_loader), elf(system_loader + b'\0'))
                for payload in rejected:
                    with self.subTest(payload=payload[-58:]):
                        image.write_bytes(payload)
                        with self.assertRaisesRegex(ValueError, 'system ELF interpreter'):
                            RELEASE.verify_images('linux', stage, *source_and_policy, portable=True)

    def files(self, platform):
        files = {name: f'data for {name}'.encode() for name in CANDIDATE.file_limits(platform)
                 if name != CANDIDATE.MANIFEST}
        if platform == 'linux':
            files[CANDIDATE.OPENSSL_LICENSE] = OPENSSL
        document = CANDIDATE.manifest(platform, REVISION, '2.40' if platform == 'linux' else None, files)
        files[CANDIDATE.MANIFEST] = json.dumps(document).encode()
        return files

    def archive(self, platform, files, path):
        with path.open('wb') as stream:
            CANDIDATE.write_package(platform, files, stream)
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def test_round_trip_install_is_fresh_and_matches_all_files(self):
        for platform in ('linux', 'windows'):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                archive = root / ('candidate' + CANDIDATE.suffix(platform))
                files = self.files(platform)
                digest = self.archive(platform, files, archive)
                prefix = root / 'clean candidate Ω'
                with patch.object(CANDIDATE, 'OPENSSL_LICENSE_SHA256', CANDIDATE.sha256(OPENSSL)):
                    if platform == 'linux' and os.name != 'posix':
                        self.assertEqual(CANDIDATE.load(platform, archive, digest)[0], files)
                        with self.assertRaisesRegex(ValueError, 'POSIX executable modes'):
                            CANDIDATE.install(platform, archive, digest, prefix)
                        self.assertFalse(prefix.exists())
                        continue
                    document, actual_digest = CANDIDATE.install(platform, archive, digest, prefix)
                    self.assertEqual(actual_digest, digest)
                    self.assertEqual(document['source_revision'], REVISION)
                    self.assertEqual({p.relative_to(prefix).as_posix(): p.read_bytes()
                                      for p in prefix.rglob('*') if p.is_file()}, files)
                    with self.assertRaises(FileExistsError):
                        CANDIDATE.install(platform, archive, digest, prefix)

    def named_payload(self, platform):
        files = self.files(platform)
        tzdb = CANDIDATE.tzdb_paths(platform)
        for name, destination in tzdb.items():
            files[destination] = f'{name} fixture contents'.encode()
        inventory = ''.join(f'{name} {hashlib.sha256(files[path]).hexdigest()}\n'
                            for name, path in sorted(tzdb.items())).encode('ascii')
        miner, preferences, stream_worker = CANDIDATE.images(platform)
        for image in (miner, preferences):
            files[f'bin/{image}'] += inventory
        files[f'bin/{miner}'] += stream_worker.encode('ascii') + b'\0'
        return files, hashlib.sha256(inventory).hexdigest()

    def test_named_tzdb_inventory_and_pins_are_checked_before_install(self):
        for platform in ('windows', 'linux'):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                archive = root / ('named' + CANDIDATE.suffix(platform))
                other = root / ('renamed' + CANDIDATE.suffix(platform))
                prefix = root / 'installed'
                files, fixture_pin = self.named_payload(platform)
                tzdb = CANDIDATE.tzdb_paths(platform)
                miner, preferences, stream_worker = CANDIDATE.images(platform)
                consumers = (f'bin/{miner}', f'bin/{preferences}')
                worker_reference = stream_worker.encode('ascii') + b'\0'
                named_only_miner = files[consumers[0]][:-len(worker_reference)]
                glibc = '2.40' if platform == 'linux' else None

                def rewrite_manifest(payloads, named_tzdb=True):
                    original = {name: data for name, data in payloads.items() if name != CANDIDATE.MANIFEST}
                    document = CANDIDATE.manifest(platform, REVISION, glibc, original, named_tzdb)
                    return {**original, CANDIDATE.MANIFEST: json.dumps(document).encode()}

                files = rewrite_manifest(files)
                with patch.dict(CANDIDATE.TZDB_INVENTORY_SHA256, {platform: fixture_pin}), \
                        patch.object(CANDIDATE, 'OPENSSL_LICENSE_SHA256', CANDIDATE.sha256(OPENSSL)):
                    digest = self.archive(platform, files, archive)
                    self.assertEqual(digest, self.archive(platform, files, other))
                    self.assertEqual(CANDIDATE.load(platform, archive, digest)[0], files)
                    for changed in (
                        rewrite_manifest({**files, tzdb['version']: b'a different database'}),
                        rewrite_manifest({key: val for key, val in files.items() if key != tzdb['version']}),
                        rewrite_manifest(files, named_tzdb=False),
                        rewrite_manifest({**files, consumers[0]: named_only_miner}),
                        *(rewrite_manifest({**files, consumer: b'older TLS-only image'})
                          for consumer in consumers),
                    ):
                        changed_digest = self.archive(platform, changed, other)
                        with self.assertRaises(ValueError):
                            CANDIDATE.install(platform, other, changed_digest, prefix)
                    self.assertFalse(prefix.exists())
                    if platform == 'windows' or os.name == 'posix':
                        document, _ = CANDIDATE.install(platform, archive, digest, prefix)
                        self.assertEqual(document['schema'], CANDIDATE.NAMED_TZDB_SCHEMA)
                        self.assertEqual((prefix / tzdb['version']).read_bytes(), files[tzdb['version']])
                        if platform == 'linux':
                            self.assertEqual(stat.S_IMODE((prefix / tzdb['version']).stat().st_mode), 0o644)

    @staticmethod
    def public_worker_payload(platform):
        backend_name = b'Schannel\0' if platform == 'windows' else b'OpenSSL 3.5.9 '
        backend = (b'schannel: AcquireCredentialsHandle failed:' if platform == 'windows' else b'OpenSSL SSL_connect:')
        return (b'CMJSON1 libcurl\0CanaryMiner-PublicData/1\0' + backend_name + backend +
                b'8.23.0\0' + b'1.34.9\0')

    def test_public_https_install_refuses_mixed_native_images_and_wrong_inventory(self):
        for platform in ('windows', 'linux'):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                archive = root / ('market' + CANDIDATE.suffix(platform))
                prefix = root / 'installed'
                files, fixture_pin = self.named_payload(platform)
                source = Path(__file__).resolve().parents[1]
                for name in CANDIDATE.PUBLIC_HTTPS_NOTICE_SHA256:
                    files[name] = (source / name).read_bytes()
                miner, preferences, pool, worker = (f'bin/{name}' for name in CANDIDATE.images(platform, True))
                offline_miner = files[miner]
                files[miner] += b'  public-https=opt-in\0'
                backend = (b'schannel: AcquireCredentialsHandle failed:' if platform == 'windows'
                           else b'OpenSSL SSL_connect:')
                files[worker] = self.public_worker_payload(platform)

                def with_manifest(payload, public_https=True):
                    data = {name: value for name, value in payload.items() if name != CANDIDATE.MANIFEST}
                    doc = CANDIDATE.manifest(platform, REVISION, '2.40' if platform == 'linux' else None,
                                             data, named_tzdb=True, public_https=public_https)
                    return {**data, CANDIDATE.MANIFEST: json.dumps(doc).encode()}

                with patch.dict(CANDIDATE.TZDB_INVENTORY_SHA256, {platform: fixture_pin}), \
                        patch.object(CANDIDATE, 'OPENSSL_LICENSE_SHA256', CANDIDATE.sha256(OPENSSL)):
                    for mixed in (
                        with_manifest({**files, miner: offline_miner}),
                        with_manifest({**files, preferences: files[preferences] + files[worker]}),
                        with_manifest({**files, pool: files[pool] + files[worker]}),
                        with_manifest({**files, worker: files[worker].replace(b'8.23.0', b'8.12.0')}),
                        with_manifest({**files, worker: files[worker].replace(b'1.34.9', b'1.34.8')}),
                        with_manifest({**files, worker: files[worker].replace(backend, b'invalid backend')}),
                        with_manifest({**files, worker: files[worker] + b'fixture.test.invalid'}),
                        with_manifest({**files, worker: files[worker] + b'MultiSSL\0'}),
                        with_manifest({**files, 'LICENSES/curl-8.23.0.txt': b'unrelated notice'}),
                        with_manifest({name: value for name, value in files.items() if name != worker}),
                        with_manifest({name: value for name, value in files.items() if name != worker}, False),
                    ):
                        digest = self.archive(platform, mixed, archive)
                        with self.assertRaises(ValueError):
                            CANDIDATE.install(platform, archive, digest, prefix)
                    self.assertFalse(prefix.exists())
                    valid = with_manifest(files)
                    digest = self.archive(platform, valid, archive)
                    _, document, _ = CANDIDATE.load(platform, archive, digest)
                    self.assertEqual(document['schema'], CANDIDATE.PUBLIC_HTTPS_SCHEMA)
                    self.assertEqual(document['public_https']['tls_backend'], document['tls_backend'])
                    if platform == 'windows' or os.name == 'posix':
                        CANDIDATE.install(platform, archive, digest, prefix)
                        self.assertEqual((prefix / worker).read_bytes(), files[worker])
                        if platform == 'linux':
                            self.assertEqual(stat.S_IMODE((prefix / worker).stat().st_mode), 0o755)

    def test_energy_cache_approval_is_explicit_and_official_release_refuses_enabled_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory) / 'CMakeCache.txt'
            base = ['CANARY_PROFILE:STRING=production-sm120', 'CMAKE_BUILD_TYPE:STRING=Release',
                    'CANARY_ENABLE_POOL_TLS_RUNTIME:BOOL=ON', 'CANARY_ENABLE_NAMED_TZDB:BOOL=OFF',
                    'CANARY_ENABLE_PUBLIC_HTTPS:BOOL=OFF', '_OPENSSL_VERSION:INTERNAL=3.5.9',
                    *(f'{key}:BOOL=OFF' for key in CANDIDATE.REQUIRED_OFF if key != CANDIDATE.ENERGY_RUNTIME)]
            for setting in ('BOOL=OFF', 'BOOL=ON', 'STRING=ON'):
                with self.subTest(setting=setting):
                    cache.write_text('\n'.join([*base, f'{CANDIDATE.ENERGY_RUNTIME}:{setting}']), encoding='utf8')
                    if setting == 'BOOL=ON':
                        CANDIDATE.check_cache(cache, 'linux', energy_runtime=True)
                    else:
                        with self.assertRaises(ValueError):
                            CANDIDATE.check_cache(cache, 'linux', energy_runtime=True)
            cache.write_text('\n'.join([*base, f'{CANDIDATE.ENERGY_RUNTIME}:BOOL=ON']), encoding='utf8')
            with self.assertRaises(ValueError):
                CANDIDATE.check_cache(cache, 'linux')
            with self.assertRaises(ValueError):
                RELEASE.check_cache(cache)
        legacy = RELEASE.ENERGY_LEASE_QUERY + RELEASE.ENERGY_AUTHORIZATION_FILE
        for indicator in (RELEASE.ENERGY_RUNTIME_MARKER, legacy):
            with patch.object(RELEASE, 'check_cache'), patch.object(
                    RELEASE, 'verify_images', return_value=((), {'canary-miner.exe': indicator})):
                with self.assertRaises(ValueError):
                    RELEASE.verify('windows', None, None, None, None, None)

    def test_energy_candidate_profiles_bind_helpers_sqlite_and_consent(self):
        for platform in ('windows', 'linux'):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                archive = root / ('energy' + CANDIDATE.suffix(platform))
                prefix = root / 'energy install'
                old = self.files(platform)
                files = {name: value for name, value in old.items() if name != CANDIDATE.MANIFEST}
                glibc = '2.40' if platform == 'linux' else None
                miner = 'bin/' + CANDIDATE.images(platform)[0]
                auth, collector = ('bin/' + name for name in CANDIDATE.images(platform, energy_runtime=True)[-2:])
                tls_ref = CANDIDATE.images(platform)[2].encode('ascii') + b'\0'
                files[miner] += RELEASE.ENERGY_RUNTIME_MARKER + tls_ref
                auth_marker, collector_marker = CANDIDATE.ENERGY_ROLE_MARKERS
                files[auth] = auth_marker + b'authorization with ["CMEPQ1",'
                sqlite = CANDIDATE.ENERGY_SQLITE[platform].encode('utf-16-le' if platform == 'windows' else 'ascii')
                files[collector] = collector_marker + b'collector with CMERQ1\n\0sqlite3_libversion_number\0' + sqlite

                def with_manifest(payload, **fields):
                    doc = CANDIDATE.manifest(platform, REVISION, glibc, payload, energy_runtime=True)
                    doc.update(fields)
                    return {**payload, CANDIDATE.MANIFEST: json.dumps(doc).encode()}

                with patch.object(CANDIDATE, 'OPENSSL_LICENSE_SHA256', CANDIDATE.sha256(OPENSSL)):
                    for mixed in (
                        with_manifest({key: value for key, value in files.items() if key != collector}),
                        with_manifest({**files, miner: old[miner]}),
                        with_manifest({**files, miner: files[miner].replace(tls_ref, b'')}),
                        with_manifest({**files, collector: files[auth]}),
                        with_manifest({**files, auth: collector_marker + b'other build with ["CMEPQ1",'}),
                        with_manifest({**files, auth: files[auth] + collector_marker}),
                        with_manifest({**files, auth: files[miner] + files[auth]}),
                        with_manifest({**files, collector: files[collector].replace(sqlite, b'other SQLite')}),
                        with_manifest({**files, auth: files[auth] + b'fixture-stall-before.json'}),
                        with_manifest(files, named_tzdb=0),
                        with_manifest(files, energy_runtime={'default_consent': 'on'}),
                    ):
                        digest = self.archive(platform, mixed, archive)
                        with self.assertRaises(ValueError):
                            CANDIDATE.install(platform, archive, digest, prefix)
                    self.assertFalse(prefix.exists())
                    legacy = {key: value for key, value in files.items() if key not in (auth, collector)}
                    for marker in (RELEASE.ENERGY_RUNTIME_MARKER,
                                   RELEASE.ENERGY_LEASE_QUERY + RELEASE.ENERGY_AUTHORIZATION_FILE):
                        legacy[miner] = old[miner] + marker
                        doc = CANDIDATE.manifest(platform, REVISION, glibc, {
                            key: value for key, value in legacy.items() if key != CANDIDATE.MANIFEST})
                        legacy[CANDIDATE.MANIFEST] = json.dumps(doc).encode()
                        with self.assertRaises(ValueError):
                            CANDIDATE.load(platform, archive, self.archive(platform, legacy, archive))
                    digest = self.archive(platform, with_manifest(files), archive)
                    _, document, _ = CANDIDATE.load(platform, archive, digest)
                    self.assertEqual(document['schema'], CANDIDATE.ENERGY_RUNTIME_SCHEMA)
                    self.assertEqual(document['energy_runtime'], {
                        'default_consent': 'off', 'sqlite_library': CANDIDATE.ENERGY_SQLITE[platform],
                        'min_sqlite': '3.31.0'})
                    if platform == 'windows' or os.name == 'posix':
                        CANDIDATE.install(platform, archive, digest, prefix)
                        self.assertEqual((prefix / auth).read_bytes(), files[auth])
                        self.assertEqual((prefix / collector).read_bytes(), files[collector])
                        if platform == 'linux':
                            self.assertEqual(stat.S_IMODE((prefix / collector).stat().st_mode), 0o755)

    def test_energy_with_named_clocks_and_optional_public_worker(self):
        for platform in ('windows', 'linux'):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as directory:
                archive = Path(directory) / ('integrated' + CANDIDATE.suffix(platform))
                files, fixture_pin = self.named_payload(platform)
                files.pop(CANDIDATE.MANIFEST, None)
                glibc = '2.40' if platform == 'linux' else None
                miner, _, _, public, auth, collector = (
                    'bin/' + name for name in CANDIDATE.images(platform, public_https=True, energy_runtime=True))
                files[miner] += RELEASE.ENERGY_RUNTIME_MARKER
                auth_marker, collector_marker = CANDIDATE.ENERGY_ROLE_MARKERS
                files[auth] = auth_marker + b'["CMEPQ1",'
                sqlite = CANDIDATE.ENERGY_SQLITE[platform].encode('utf-16-le' if platform == 'windows' else 'ascii')
                files[collector] = collector_marker + b'CMERQ1\n\0sqlite3_libversion_number\0' + sqlite
                for name in CANDIDATE.PUBLIC_HTTPS_NOTICE_SHA256:
                    files[name] = (Path(__file__).resolve().parents[1] / name).read_bytes()

                def check(payload, public_https, expected, billing_watch=False):
                    doc = CANDIDATE.manifest(platform, REVISION, glibc, payload, named_tzdb=True,
                                             public_https=public_https, energy_runtime=True, billing_watch=billing_watch)
                    digest = self.archive(platform, {**payload, CANDIDATE.MANIFEST: json.dumps(doc).encode()}, archive)
                    if expected:
                        self.assertEqual(CANDIDATE.load(platform, archive, digest)[1], doc)
                    else:
                        with self.assertRaises(ValueError):
                            CANDIDATE.load(platform, archive, digest)

                with patch.dict(CANDIDATE.TZDB_INVENTORY_SHA256, {platform: fixture_pin}), \
                        patch.object(CANDIDATE, 'OPENSSL_LICENSE_SHA256', CANDIDATE.sha256(OPENSSL)):
                    check(files, False, True)
                    files[miner] += CANDIDATE.PUBLIC_HTTPS_OPT_IN
                    files[public] = self.public_worker_payload(platform)
                    check(files, True, True)
                    for unexpected in (auth, collector):
                        check({**files, unexpected: files[unexpected] + CANDIDATE.PUBLIC_HTTPS_AGENT}, True, False)
                    helper = CANDIDATE.billing_watch_path(platform)
                    supplied = {**files, helper: b'billing watch deployment script'}
                    check(supplied, True, False)
                    check(supplied, True, False, billing_watch=True)  # Old miner.
                    files[miner] += CANDIDATE.BILLING_WATCH_QUIET
                    check(files, True, False, billing_watch=True)  # Missing helper.
                    check({**files, helper: supplied[helper]}, True, True, billing_watch=True)

    def test_billing_watch_installs_only_its_platform_helper_and_requires_named_clocks(self):
        for platform in ('windows', 'linux'):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                archive = root / ('watch' + CANDIDATE.suffix(platform))
                files, fixture_pin = self.named_payload(platform)
                files.pop(CANDIDATE.MANIFEST, None)
                glibc = '2.40' if platform == 'linux' else None
                miner = 'bin/' + CANDIDATE.images(platform)[0]
                files[miner] += CANDIDATE.BILLING_WATCH_QUIET
                helper = CANDIDATE.billing_watch_path(platform)
                other_helper = CANDIDATE.billing_watch_path('linux' if platform == 'windows' else 'windows')
                files[helper] = b'billing watch script'

                def publish(payload, named):
                    doc = CANDIDATE.manifest(platform, REVISION, glibc, payload, named_tzdb=named, billing_watch=True)
                    return self.archive(platform, {**payload, CANDIDATE.MANIFEST: json.dumps(doc).encode()}, archive)

                with patch.dict(CANDIDATE.TZDB_INVENTORY_SHA256, {platform: fixture_pin}), \
                        patch.object(CANDIDATE, 'OPENSSL_LICENSE_SHA256', CANDIDATE.sha256(OPENSSL)):
                    for bad, named in (({**files, other_helper: files[helper]}, True), (files, False)):
                        digest = publish(bad, named)
                        with self.assertRaises(ValueError):
                            CANDIDATE.load(platform, archive, digest)
                    digest = publish(files, True)
                    if platform == 'windows' or os.name == 'posix':
                        prefix = root / 'installation'
                        doc, _ = CANDIDATE.install(platform, archive, digest, prefix)
                        self.assertEqual(doc['schema'], CANDIDATE.BILLING_WATCH_SCHEMA)
                        self.assertEqual((prefix / helper).read_bytes(), files[helper])
                        self.assertFalse((prefix / other_helper).exists())
                        if platform == 'linux':
                            self.assertEqual(stat.S_IMODE((prefix / helper).stat().st_mode), 0o755)

    def test_account_capture_requires_each_companion_and_preserves_v5(self):
        for platform in ('windows', 'linux'):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                archive = root / ('account' + CANDIDATE.suffix(platform))
                files, fixture_pin = self.named_payload(platform)
                files.pop(CANDIDATE.MANIFEST, None)
                glibc = '2.40' if platform == 'linux' else None
                miner = 'bin/' + CANDIDATE.images(platform)[0]
                files[miner] += CANDIDATE.BILLING_WATCH_QUIET
                helper = CANDIDATE.billing_watch_path(platform)
                files[helper] = b'watch script'
                source = Path(__file__).resolve().parents[1]
                companions = {name: (source / name).read_bytes() for name in CANDIDATE.OCTOPUS_ACCOUNT_FILES}

                def ship(payload, *, account=False):
                    document = CANDIDATE.manifest(platform, REVISION, glibc, payload, named_tzdb=True,
                                                  billing_watch=True, octopus_account=account)
                    digest = self.archive(platform, {**payload, CANDIDATE.MANIFEST: json.dumps(document).encode()}, archive)
                    return digest

                with patch.dict(CANDIDATE.TZDB_INVENTORY_SHA256, {platform: fixture_pin}), \
                        patch.object(CANDIDATE, 'OPENSSL_LICENSE_SHA256', CANDIDATE.sha256(OPENSSL)):
                    v5 = ship(files)
                    self.assertEqual(CANDIDATE.load(platform, archive, v5)[1]['schema'], CANDIDATE.BILLING_WATCH_SCHEMA)
                    combined = {**files, **companions}
                    with self.assertRaises(ValueError):
                        CANDIDATE.load(platform, archive, ship(combined))
                    for name in (*companions, helper):
                        incomplete = {key: val for key, val in combined.items() if key != name}
                        with self.assertRaises(ValueError):
                            CANDIDATE.load(platform, archive, ship(incomplete, account=True))
                    digest = ship(combined, account=True)
                    if platform == 'windows' or os.name == 'posix':
                        prefix = root / 'installed'
                        doc, _ = CANDIDATE.install(platform, archive, digest, prefix)
                        self.assertEqual(doc['schema'], CANDIDATE.OCTOPUS_ACCOUNT_SCHEMA)
                        for name, data in companions.items():
                            self.assertEqual((prefix / name).read_bytes(), data)
                            if platform == 'linux':
                                self.assertEqual(stat.S_IMODE((prefix / name).stat().st_mode), 0o644)

    def test_refuses_wrong_hash_changed_file_extra_and_missing_members(self):
        for platform in ('linux', 'windows'):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                archive = root / ('candidate' + CANDIDATE.suffix(platform))
                files = self.files(platform)
                self.archive(platform, files, archive)
                prefix = root / 'refused'
                with self.assertRaisesRegex(ValueError, 'archive hash'):
                    CANDIDATE.install(platform, archive, '0' * 64, prefix)
                for modified in (
                    {**files, 'README.txt': b'changed payload'},
                    {**files, 'CONFIG.TLS.EXAMPLE.JSON': b'extra'},
                    {**files, 'bin/extra': b'extra'},
                    {name: value for name, value in files.items() if 'pool_tls_stream_worker' not in name},
                ):
                    digest = self.archive(platform, modified, archive)
                    with patch.object(CANDIDATE, 'OPENSSL_LICENSE_SHA256', CANDIDATE.sha256(OPENSSL)):
                        with self.assertRaises(ValueError):
                            CANDIDATE.install(platform, archive, digest, prefix)
                self.assertFalse(prefix.exists())

    def test_refuses_duplicate_and_nonregular_zip_member(self):
        files = self.files('windows')
        for mode in (stat.S_IFREG | 0o644, stat.S_IFLNK | 0o644):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'candidate.zip'
                self.archive('windows', files, path)
                with zipfile.ZipFile(path, 'a') as zip_file, warnings.catch_warnings():
                    warnings.simplefilter('ignore', UserWarning)
                    entry = zipfile.ZipInfo('README.txt')
                    entry.create_system = 3
                    entry.external_attr = mode << 16
                    zip_file.writestr(entry, b'replacement')
                with self.assertRaises(ValueError):
                    CANDIDATE.load('windows', path)

    def test_cli_refuses_bad_compressed_stream_without_traceback(self):
        damaged = bytearray(gzip.compress(b'candidate archive', mtime=0))
        damaged[10] = 0b111  # Invalid DEFLATE block type.
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'candidate.tar.gz'
            archive.write_bytes(damaged)
            result = subprocess.run(
                [sys.executable, '-B', CANDIDATE.__file__, 'verify', '--platform', 'linux',
                 '--archive', str(archive), '--sha256', hashlib.sha256(damaged).hexdigest()],
                capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 1)
            self.assertFalse(result.stdout)
            self.assertTrue(result.stderr.startswith('tls_candidate: verify refused'))
            self.assertNotIn('Traceback', result.stderr)
            self.assertNotIn(str(archive.parent), result.stderr)

    def test_too_large_expanded_tar_refused_before_parsing(self):
        archive = io.BytesIO()
        with patch.object(CANDIDATE, 'MAX_PACKAGE_BYTES', 8192):
            files = self.files('linux')
            files['README.txt'] = b'x' * (256 * 1024)
            CANDIDATE.write_package('linux', files, archive)
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'candidate.tar.gz'
                path.write_bytes(archive.getvalue())
                with self.assertRaisesRegex(ValueError, 'expanded size'):
                    CANDIDATE.load('linux', path)


if __name__ == '__main__':
    unittest.main()
