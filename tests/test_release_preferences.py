"""Archive-software contracts only; never build/run a miner, CUDA toolchain or release builder."""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
RELEASE = ROOT / 'packaging' / 'release'
SPEC = importlib.util.spec_from_file_location('release_verifier', RELEASE / 'verify_artifacts.py')
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)
CHECK_SPEC = importlib.util.spec_from_file_location('release_fixture_checks', ROOT / 'tools/check_secrets.py')
CHECKS = importlib.util.module_from_spec(CHECK_SPEC)
CHECK_SPEC.loader.exec_module(CHECKS)
PRIVATE = 'release-fixture-private-token'
PUBLIC_FEE = 'prl1' + 'abcdef' * 9


def image(platform, payload=b''):
    data = bytearray(128)
    if platform == 'linux':
        data[:6] = b'\x7fELF\x02\x01'
        data[18:20] = b'\x3e\x00'
    else:
        data[:2] = b'MZ'
        data[60:64] = (64).to_bytes(4, 'little')
        data[64:70] = b'PE\0\0\x64\x86'
        data[88:90] = b'\x0b\x02'
    return bytes(data) + payload


class ReleaseArtifactTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='canary-release-verify-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.stage = self.root / 'owned stage with spaces'
        self.stage.mkdir()
        self.source = self.root / 'source'
        (self.source / 'tools').mkdir(parents=True)
        (self.source / 'src/product').mkdir(parents=True)
        shutil.copyfile(ROOT / 'tools/check_secrets.py', self.source / 'tools/check_secrets.py')
        # Synthetic public metadata, never a real wallet or account.
        (self.source / 'src/product/fee.h').write_text('#define CANARY_FEE_DESTINATION "' + PUBLIC_FEE + '"\n', encoding='utf8')
        self.cache = self.root / 'CMakeCache.txt'
        self.cache.write_text('CANARY_ENABLE_ENERGY_RUNTIME:BOOL=OFF\nCANARY_ENABLE_WORKER_CONTROL_RUNTIME:BOOL=OFF\nCANARY_FEE_TEST_POLICY:BOOL=OFF\nCANARY_ENABLE_PUBLIC_HTTPS:BOOL=OFF\n', encoding='utf8')
        self.deny = self.root / 'owned-deny.txt'
        self.deny.write_text(PRIVATE + '\n', encoding='utf8')

    def write_images(self, platform, payload=b''):
        suffix = '.exe' if platform == 'windows' else ''
        self.names = tuple(name + suffix for name in ('canary-miner', 'canary_preferences_worker'))
        for name in self.names:
            (self.stage / name).write_bytes(image(platform, payload))

    def metadata(self, argv, **kwargs):
        self.assertEqual(argv[0], 'owned-inspector')
        self.assertTrue(kwargs['check'] and kwargs['capture_output'])
        self.assertEqual(kwargs['timeout'], 30)
        self.assertNotIn('shell', kwargs)
        if argv[1] == '-p':
            output = '  NEEDED libc.so.6\n  NEEDED libstdc++.so.6\n'
        elif argv[1] == '-T':
            output = '0000 DF *UND* 0000 (GLIBC_2.39) pidfd_spawn\n0000 DF *UND* 0000 (GLIBC_2.41) other\n'
        else:
            self.assertEqual(argv[1], '/DEPENDENTS')
            output = 'File Type: EXECUTABLE IMAGE\n\n  Image has the following dependencies:\n\n    KERNEL32.dll\n    VCRUNTIME140.dll\n    api-ms-win-crt-runtime-l1-1-0.dll\n\n  Summary\n'
        return SimpleNamespace(stdout=output)

    def verify(self, platform):
        return VERIFY.verify(platform, self.stage, self.source, self.cache, self.deny, 'owned-inspector')

    def test_both_artifacts_are_read_only_inspected_and_larger_floor_wins(self):
        for platform in ('linux', 'windows'):
            with self.subTest(platform=platform):
                self.write_images(platform)
                before = {name: (self.stage / name).read_bytes() for name in self.names}
                with mock.patch.object(VERIFY.subprocess, 'run', side_effect=self.metadata) as run:
                    self.assertEqual(self.verify(platform), '2.41' if platform == 'linux' else '')
                self.assertEqual(run.call_count, 4 if platform == 'linux' else 2)
                self.assertEqual({Path(call.args[0][-1]).name for call in run.call_args_list}, set(self.names))
                self.assertEqual(before, {name: (self.stage / name).read_bytes() for name in self.names})

    def test_missing_or_invalid_helper_refuses_before_any_inspection(self):
        self.write_images('linux')
        helper = self.stage / self.names[1]
        for replacement in (None, b'', b'not-an-ELF' * 30, image('windows')):
            with self.subTest(replacement=type(replacement).__name__):
                if helper.exists():
                    helper.unlink()
                if replacement is not None:
                    helper.write_bytes(replacement)
                with mock.patch.object(VERIFY.subprocess, 'run', side_effect=AssertionError('must not inspect')):
                    with self.assertRaises((OSError, ValueError)):
                        self.verify('linux')

    @unittest.skipUnless(os.name == 'posix', 'POSIX symlink fixture; Windows reparse checks are separate')
    def test_symlinked_helper_is_never_adopted(self):
        self.write_images('linux')
        helper = self.stage / self.names[1]
        helper.unlink()
        helper.symlink_to(self.stage / self.names[0])
        with self.assertRaises(ValueError):
            self.verify('linux')

    def test_cache_missing_duplicate_enabled_and_future_development_flags_refuse(self):
        original = self.cache.read_text(encoding='utf8')
        for text in ('', original.replace('CANARY_FEE_TEST_POLICY:BOOL=OFF', 'CANARY_FEE_TEST_POLICY:BOOL=ON'),
                     original + 'CANARY_ENABLE_ENERGY_RUNTIME:BOOL=OFF\n',
                     original + 'CANARY_ENABLE_FUTURE_HELPER:BOOL=ON\n'):
            with self.subTest(text_size=len(text)):
                self.cache.write_text(text, encoding='utf8')
                with self.assertRaises(ValueError):
                    VERIFY.check_cache(self.cache)

    def test_duplicate_fee_on_and_non_bool_capability_cannot_hide_behind_an_off_line(self):
        original = self.cache.read_text(encoding='utf8')
        for extra in ('CANARY_FEE_TEST_POLICY:BOOL=ON\n', 'CANARY_ENABLE_PUBLIC_HTTPS:STRING=ON\n'):
            with self.subTest(extra=extra.split(':')[0]):
                self.cache.write_text(original + extra, encoding='utf8')
                with self.assertRaises(ValueError):
                    VERIFY.check_cache(self.cache)

    def test_private_content_in_either_artifact_refuses_without_echoing_bytes(self):
        for platform in ('linux', 'windows'):
            for role in range(2):
                for encoding in ('utf8', 'utf-16-le', 'utf-16-be'):
                    for offset in (0, 1):
                        with self.subTest(platform=platform, role=role, encoding=encoding, offset=offset):
                            self.write_images(platform)
                            target = self.stage / self.names[role]
                            target.write_bytes(image(platform, b'\0' * offset + PRIVATE.encode(encoding)))
                            with mock.patch.object(VERIFY.subprocess, 'run', side_effect=self.metadata):
                                with self.assertRaises(ValueError) as caught:
                                    self.verify(platform)
                            self.assertNotIn(PRIVATE, str(caught.exception))

    def test_home_paths_and_short_private_literals_are_not_hidden_by_ascii_run_cutoff(self):
        for original in ('/home/user/fixture', 'C:/Users/user/fixture', '/mnt/c/fixture'):
            for encoding in ('utf8', 'utf-16-le', 'utf-16-be'):
                with self.subTest(encoding=encoding):
                    with self.assertRaises(ValueError):
                        VERIFY.privacy_check(original.encode(encoding), 'fixture', CHECKS, [], PUBLIC_FEE)
        with self.assertRaises(ValueError):
            VERIFY.privacy_check(b'\0xyz\0', 'fixture', CHECKS, ['xyz'], PUBLIC_FEE)

    def test_only_exact_public_fee_and_approved_handle_tokens_are_exempt(self):
        VERIFY.privacy_check(PUBLIC_FEE.encode(), 'fixture', CHECKS, [PUBLIC_FEE], PUBLIC_FEE)
        with self.assertRaises(ValueError):
            VERIFY.privacy_check((PUBLIC_FEE[:-1] + '0').encode(), 'fixture', CHECKS, [], PUBLIC_FEE)
        with self.assertRaises(ValueError):
            VERIFY.privacy_check((PUBLIC_FEE + 'private').encode(), 'fixture', CHECKS, [], PUBLIC_FEE)
        for token in CHECKS.PUBLIC_TOKENS:
            VERIFY.privacy_check(token.encode(), 'fixture', CHECKS, [token], PUBLIC_FEE)
            with self.assertRaises(ValueError):
                VERIFY.privacy_check((token + '_private').encode(), 'fixture', CHECKS, [token + '_private'], PUBLIC_FEE)

    def test_linux_fixed_floor_no_pidfd_no_glibc_unknown_and_toolkit_dependencies(self):
        symbols = '0000 *UND* 0000 (GLIBC_2.39) pidfd_spawn\n'
        self.assertEqual(VERIFY.dependency_check('linux', 'canary-miner', ' NEEDED libc.so.6\n', symbols), (2, 40))
        for name in ('canary-miner', 'canary_preferences_worker'):
            for dependency in ('libcudart.so.13', 'libssl.so.3', 'unknown.so'):
                with self.subTest(name=name, dependency=dependency):
                    with self.assertRaises(ValueError):
                        VERIFY.dependency_check('linux', name, ' NEEDED ' + dependency + '\n', symbols)
        for bad_symbols in ('', '0000 *UND* 0000 (GLIBC_2.41) other\n', '0000 defined GLIBC_2.39 pidfd_spawn\n'):
            with self.assertRaises(ValueError):
                VERIFY.dependency_check('linux', 'canary-miner', ' NEEDED libc.so.6\n', bad_symbols)
        self.assertEqual(VERIFY.dependency_check('linux', 'canary-miner', ' NEEDED libcuda.so.1\n', symbols), (2, 40))
        with self.assertRaises(ValueError):
            VERIFY.dependency_check('linux', 'canary_preferences_worker', ' NEEDED libcuda.so.1\n', symbols)

    def test_windows_dependencies_include_delay_imports_and_forbid_debug_toolkit_helper_gpu(self):
        for name in ('canary-miner.exe', 'canary_preferences_worker.exe'):
            for dependency in ('cudart64_13.dll', 'MSVCP140D.dll', 'ucrtbased.dll', 'unknown.dll'):
                with self.subTest(name=name, dependency=dependency):
                    with self.assertRaises(ValueError):
                        VERIFY.dependency_check('windows', name, ' Image has the following dependencies:\n\n KERNEL32.dll\n\n Image has the following delay load dependencies:\n\n ' + dependency + '\n')
        self.assertEqual(VERIFY.dependency_check('windows', 'canary-miner.exe', ' Image has the following dependencies:\n\n NVCUDA.dll\n'), ())
        with self.assertRaises(ValueError):
            VERIFY.dependency_check('windows', 'canary_preferences_worker.exe', ' Image has the following dependencies:\n\n NVCUDA.dll\n')
        with self.assertRaises(ValueError):
            VERIFY.dependency_check('windows', 'canary-miner.exe', 'unreadable metadata')

    def test_inspector_failure_never_echoes_command_output_or_retries(self):
        self.write_images('windows')
        for error in (OSError(PRIVATE), subprocess.TimeoutExpired('fixture', 30, PRIVATE.encode()),
                      subprocess.CalledProcessError(1, 'fixture', PRIVATE.encode())):
            with self.subTest(error=type(error).__name__):
                with mock.patch.object(VERIFY.subprocess, 'run', side_effect=error) as run:
                    with self.assertRaises(ValueError) as caught:
                        self.verify('windows')
                self.assertNotIn(PRIVATE, str(caught.exception))
                self.assertEqual(run.call_count, 1)

    def test_changed_artifact_during_inspection_is_preserved_and_refused(self):
        self.write_images('windows')
        target = self.stage / self.names[0]
        def change(argv, **kwargs):
            target.write_bytes(image('windows', b'changed'))
            return self.metadata(argv, **kwargs)
        with mock.patch.object(VERIFY.subprocess, 'run', side_effect=change):
            with self.assertRaises(ValueError):
                self.verify('windows')
        self.assertTrue(target.read_bytes().endswith(b'changed'))

    def test_late_second_inspection_cannot_hide_an_earlier_artifact_change(self):
        self.write_images('windows')
        def change(argv, **kwargs):
            if Path(argv[-1]).name == self.names[1]:
                (self.stage / self.names[0]).write_bytes(image('windows', b'late-change'))
            return self.metadata(argv, **kwargs)
        with mock.patch.object(VERIFY.subprocess, 'run', side_effect=change):
            with self.assertRaises(ValueError):
                self.verify('windows')

    def test_empty_missing_and_invalid_utf8_private_lists_refuse_and_do_not_change_environment(self):
        self.write_images('windows')
        original = dict(os.environ)
        for data in (b'', b'# comments only\n', b'\xff\xfe'):
            self.deny.write_bytes(data)
            with self.assertRaises(ValueError):
                self.verify('windows')
            self.assertEqual(dict(os.environ), original)
        self.deny.unlink()
        with self.assertRaises(OSError):
            self.verify('windows')

    def test_dependency_names_with_spaces_paths_or_missing_dll_suffix_do_not_escape_validation(self):
        symbols = '0000 *UND* 0000 (GLIBC_2.39) pidfd_spawn\n'
        for value in ('bad name.so', '/private/path.so', 'libc.so.6 extra'):
            with self.assertRaises(ValueError):
                VERIFY.dependency_check('linux', 'canary-miner', ' NEEDED libc.so.6\n NEEDED ' + value + '\n', symbols)
        for value in ('bad name.dll', 'C:/private/path.dll', 'no-extension'):
            with self.assertRaises(ValueError):
                VERIFY.dependency_check('windows', 'canary-miner.exe', ' Image has the following dependencies:\n\n KERNEL32.dll\n ' + value + '\n')

    def test_cli_refusal_is_private_and_preserves_missing_helper_inputs(self):
        self.write_images('windows', PRIVATE.encode())
        before = {name: (self.stage / name).read_bytes() for name in self.names}
        args = ['verify_artifacts.py', '--platform', 'windows', '--stage', str(self.stage), '--source', str(self.source),
                '--cache', str(self.cache), '--deny-file', str(self.deny), '--tool', 'must-not-execute']
        out, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', args), contextlib.redirect_stdout(out), contextlib.redirect_stderr(error):
            self.assertEqual(VERIFY.main(), 1)
        self.assertEqual(out.getvalue(), '')
        self.assertNotIn(PRIVATE, error.getvalue())
        self.assertIn('Private details withheld', error.getvalue())
        self.assertEqual(before, {name: (self.stage / name).read_bytes() for name in self.names})


class PreferenceReleaseContract(unittest.TestCase):
    def test_actual_artifact_copy_fragment_keeps_both_fixed_siblings(self):
        # Only the artifact-copy/strip fragment executes: no CUDA build, archive, recursive deletion or publishing.
        with tempfile.TemporaryDirectory(prefix='canary-release-copy-') as directory:
            work = Path(directory)
            if os.name == 'nt':
                native, helper = work / 'canary-miner.exe', work / 'canary_preferences_worker.exe'
                native.write_bytes(b'owned synthetic miner fixture, not executable')
                helper.write_bytes(b'owned synthetic helper fixture, not executable')
                text = (RELEASE / 'make_windows_release.ps1').read_text(encoding='utf8')
                fragment = text[text.index('$name ='):text.index("foreach ($f in 'start.bat'")]
                script = work / 'copy-only.ps1'
                script.write_text("$ErrorActionPreference='Stop'\n$Version='fixture'\n$WorkDir=$PSScriptRoot\n$exe=Join-Path $WorkDir 'canary-miner.exe'\n" + fragment, encoding='utf8')
                command = ['pwsh', '-NoProfile', '-NonInteractive', '-File', str(script)]
            elif sys.platform.startswith('linux'):
                native, helper = work / 'canary-miner', work / 'canary_preferences_worker'
                for path in (native, helper):
                    shutil.copy2('/bin/true', path)  # owned CPU artifacts, copied/stripped but never executed
                text = (RELEASE / 'make_linux_release.sh').read_text(encoding='utf8')
                fragment = text[text.index('name="canaryminer-'):text.index('# Metadata-only inspection')]
                script = work / 'copy-only.sh'
                script.write_text('set -euo pipefail\nwork="$1"\nnative="$work/canary-miner"\nversion=fixture\n' + fragment, encoding='utf8')
                command = ['bash', str(script), str(work)]
            else:
                self.skipTest('actual copy fragment is Windows/Linux only')
            originals = (native.read_bytes(), helper.read_bytes())
            result = subprocess.run(command, capture_output=True, timeout=20)
            self.assertEqual(result.returncode, 0, 'actual copy fragment failed; private tool output withheld')
            stage = work / 'stage/canaryminer-fixture'
            self.assertEqual({path.name for path in stage.iterdir()}, {native.name, helper.name})
            self.assertEqual((native.read_bytes(), helper.read_bytes()), originals)
            self.assertGreater((stage / helper.name).stat().st_size, 0)

    def test_linux_requires_copies_and_checks_the_fixed_helper(self):
        text = (RELEASE / 'make_linux_release.sh').read_text(encoding='utf8')
        self.assertTrue('canary_preferences_worker' in text, 'mandatory Linux preference helper omitted')
        self.assertTrue('verify_artifacts.py' in text, 'both-artifact verifier omitted')
        self.assertTrue('ldd ' not in text, 'must inspect metadata without executing an inspected ELF')
        self.assertTrue('"$stage/canary_preferences_worker"' in text, 'fixed helper must be staged')

    def test_windows_requires_copies_and_checks_the_fixed_helper(self):
        text = (RELEASE / 'make_windows_release.ps1').read_text(encoding='utf8')
        self.assertTrue('canary_preferences_worker.exe' in text, 'mandatory Windows preference helper omitted')
        self.assertTrue('verify_artifacts.py' in text, 'both-artifact verifier omitted')
        self.assertTrue('dumpbin.exe' in text, 'Windows dependency inspector omitted')

    def test_readmes_do_not_promise_an_older_unqualified_native_runtime(self):
        linux = (RELEASE / 'README-linux.txt').read_text(encoding='utf8')
        self.assertTrue('Ubuntu 22.04, 24.04' not in linux, 'unqualified older-runtime promise')
        self.assertIn('glibc 2.40', linux)
        self.assertIn('pidfd', linux)
        for name in ('README-linux.txt', 'README-windows.txt'):
            with self.subTest(name=name):
                self.assertIn('canary_preferences_worker', (RELEASE / name).read_text(encoding='utf8'))


if __name__ == '__main__':
    unittest.main()
