"""Compile actual CLI with inert factories; no CUDA runtime, network or mining."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
# Emitted legacy help at parent95c6579, including printf's literal percent.
LEGACY_HELP_SHA256 = '0f07921607520497e855fc29b1d76158f2a808ba177d728539740f42c4bdfdb0'


class CanaryOperatorHelpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('g++')
        if not compiler:
            raise RuntimeError('host g++ required; missing tools are not passing skips')
        cls.stage = tempfile.TemporaryDirectory(prefix='canary-help-cli-')
        cls.addClassCleanup(cls.stage.cleanup)
        cls.binaries = {}
        for profile in ('0', '1'):
            binary = Path(cls.stage.name) / ('cli-' + profile)
            command = [compiler, '-x', 'c++', '-std=c++17', '-O1', '-Wall', '-Wextra',
                '-Werror', '-Wno-unused-variable', '-Itests/pearl_cli_stubs',
                '-DCANARY_PEARL_ONLY_BUILD=' + profile,
                '-include', 'tests/pearl_cli_stubs/cuda_stub.h', 'src/core/miner.cu',
                'src/core/registry.cu', 'tests/test_pearl_cli_stubs.cpp', '-o', str(binary)]
            compiled = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=90)
            if compiled.returncode:
                raise RuntimeError(compiled.stderr.decode(errors='replace'))
            cls.binaries[profile] = binary

    def cli(self, *args, profile='1'):
        result = subprocess.run([str(self.binaries[profile]), *args],
                                cwd=ROOT, capture_output=True, timeout=5)
        self.assertNotEqual(result.returncode, 97, result.stderr)
        self.assertNotIn(b'FORBIDDEN_DEVICE_SETUP', result.stdout + result.stderr)
        return result

    def test_help_both_spellings_exit_without_device_access(self):
        long = self.cli('--help')
        short = self.cli('-h')
        self.assertEqual((long.returncode, short.returncode), (0, 0))
        self.assertEqual(long.stdout, short.stdout)
        self.assertEqual(long.stderr + short.stderr, b'')

    def test_pearl_help_is_truthful_and_retains_real_defaults(self):
        help_text = self.cli('--help').stdout.decode()
        for text in ('CanaryMiner (CUDA) - Pearl-only development build',
                'pearl-pow only (default pearl-pow)', 'worker name (default canary)',
                'logical work-unit budget (default 4194304)', 'fixed synthetic fixture height 1851444',
                'canary-miner list-devices', 'base-before-penalty|final-adjusted-bound',
                'does not establish accepted shares or earnings', 'canary-miner selftest',
                'canary-miner status', '--config FILE', '--api-port N', "Supported devices are listed by 'list-devices'"):
            with self.subTest(text=text):
                self.assertIn(text, help_text)

    def test_pearl_help_does_not_advertise_other_algorithms_or_unsupported_options(self):
        help_text = self.cli('--help').stdout.decode()
        for text in ('Ergo', 'Autolykos', 'KawPow', 'Vulkan', 'AMD:', '+2.3%',
                '--node', '--port', '--api-key', '--nvrtc', '--cache-dag',
                '--gateway', '--bench-epoch-secs'):
            with self.subTest(text=text):
                self.assertNotIn(text, help_text)

    def test_non_pearl_and_unknown_option_refusals(self):
        for args, expected in ((['--algo', 'autolykos2'], b'supports only pearl-pow'),
                (['--algo', 'kawpow'], b'supports only pearl-pow'),
                (['--unknown-option'], b'unknown option'),
                (['--gateway', '127.0.0.1:1'], b'unknown option'), (['--node', 'x'], b'unknown option'),
                (['--port', '1'], b'unknown option'), (['--api-key', 'x'], b'unknown option'),
                (['--nvrtc'], b'unknown option'), (['--cache-dag', 'on'], b'unknown option')):
            with self.subTest(args=args):
                result = self.cli(*args)
                self.assertEqual(result.returncode, 1)
                self.assertIn(expected, result.stderr)

    def test_unsupported_source_options_still_refused_before_setup(self):
        for args, expected in ((['--pool', 'offline.invalid:1'], b'requires explicit --pearl-network'),):
            with self.subTest(args=args):
                result = self.cli(*args)
                self.assertEqual(result.returncode, 1)
                self.assertIn(expected, result.stderr)

    def test_subcommands_that_need_no_pool(self):
        version = self.cli('version')
        self.assertEqual(version.returncode, 0)
        self.assertIn(b'canaryminer 1.0.0', version.stdout)
        # the fee destination is public (H-018b): the version line carries its 64-hex SHA-256
        self.assertRegex(version.stdout, rb'fee_destination_sha256=[0-9a-f]{64}\n')
        self.assertEqual(self.cli('--version').stdout, version.stdout)
        unknown = self.cli('frobnicate')
        self.assertEqual(unknown.returncode, 2)
        self.assertIn(b"unknown command 'frobnicate'", unknown.stderr)
        for args in (['list-devices'], ['--list-devices'], ['selftest']):
            with self.subTest(args=args):
                result = subprocess.run([str(self.binaries['1']), *args], cwd=ROOT, capture_output=True, timeout=5)
                # list-devices and selftest need no pool or wallet: they go straight to device enumeration (the stub exits 97)
                self.assertEqual(result.returncode, 97, result.stderr)
                self.assertIn(b'FORBIDDEN_DEVICE_SETUP', result.stderr)

    def test_status_reports_a_missing_miner_with_its_own_exit_code(self):
        result = self.cli('status', '--api-port', '1')
        self.assertEqual(result.returncode, 6)
        self.assertIn(b'no miner is listening', result.stderr)

    def test_config_errors_exit_3_before_any_device_access(self):
        with tempfile.TemporaryDirectory() as d:
            for name, text, expected in (('missing.json', None, b'cannot read config file'),
                    ('v2.json', '{"schema_version": 2}', b'no automatic migration'),
                    ('typo.json', '{"schema_version": 1, "walet": "x"}', b"unknown key 'walet'")):
                path = Path(d) / name
                if text is not None:
                    path.write_text(text)
                with self.subTest(name=name):
                    result = self.cli('--config', str(path))
                    self.assertEqual(result.returncode, 3)
                    self.assertIn(expected, result.stderr)

    def test_config_supplies_pool_and_policy_and_cli_overrides_it(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'ok.json'
            path.write_text('{"schema_version": 1, "wallet": "not-a-pearl-address", "pools": ["kryptex"]}')
            # pool + policy come from the file, so the first problem found is the (deliberately invalid) wallet
            result = self.cli('--config', str(path))
            self.assertEqual(result.returncode, 1)
            self.assertIn(b'invalid Pearl wallet', result.stderr)
            # a CLI wallet overrides the file's, and a bad pool spec is refused up front
            self.assertEqual(self.cli('--config', str(path), '--pool', 'no-port').returncode, 3)

    def test_malformed_or_missing_pearl_policies_still_refused(self):
        for args, expected in ((['--pearl-network', 'not-a-network'], b'--pearl-network requires one'),
                (['--pearl-target-semantics', 'not-a-policy'], b'--pearl-target-semantics requires one'),
                (['--pool', 'offline.invalid:1', '--pearl-network', 'mainnet'], b'requires explicit --pearl-network'),
                (['--pool', 'offline.invalid:1', '--pearl-network', 'mainnet', '--pearl-target-semantics', 'base-before-penalty'], b'requires --wallet'),
                (['--pool', 'kryptex'], b'requires --wallet'),
                (['--bench', '--bench-height', '1'], b'requires its fixed fixture height')):
            with self.subTest(args=args):
                result = self.cli(*args)
                self.assertEqual(result.returncode, 1)
                self.assertIn(expected, result.stderr)


if __name__ == '__main__':
    unittest.main()
