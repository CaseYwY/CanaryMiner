"""Actual miner CLI admission with inert factories; no GPU, network, or mining."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class ExactProfileCliTests(unittest.TestCase):
    def test_exact_profile_reaches_owned_guard_and_bad_tuples_do_not(self):
        with tempfile.TemporaryDirectory(prefix='canary-exact-cli-') as td:
            binary = Path(td) / 'cli'
            command = ['g++', '-x', 'c++', '-std=c++17', '-O1', '-Wall', '-Wextra',
                '-Werror', '-Wno-unused-variable', '-Itests/pearl_cli_stubs',
                '-DCANARY_PEARL_ONLY_BUILD=1', '-DCANARY_PEARL_ENABLE_SM120_NATIVE_EVIDENCE=1',
                '-DCANARY_PEARL_ENABLE_SHAPE_EXPERIMENT=1', '-DPEARL_CLI_SHAPE_CAPABLE_STUB=1',
                '-DCANARY_PEARL_EXPERIMENT_COMMON_DIM=8192', '-DCANARY_PEARL_EXPERIMENT_LARGE_SHAPES=1',
                '-include', 'tests/pearl_cli_stubs/cuda_stub.h', 'src/core/miner.cu',
                'src/core/registry.cu', 'tests/test_pearl_cli_stubs.cpp', 'src/product/exact_decimal.cpp',
                'src/product/manual_market.cpp', '-pthread', '-o', str(binary)]
            result = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stderr)
            baseline = {k:v for k,v in os.environ.items() if not k.startswith('CANARY_PEARL_')}
            baseline.update(CANARY_PEARL_LOCAL_RETIREMENTS='1', CANARY_PEARL_EVIDENCE_RUN_ID='1'*32,
                CANARY_PEARL_NATIVE_EVIDENCE='sm120-generic-v1', CANARY_PEARL_ACCEPTED_QUOTA='3',
                CANARY_PEARL_SHAPE_EXPERIMENT='exact16384x131072-k8192-v1',
                CANARY_PEARL_SHAPE='16384x131072', CANARY_PEARL_TILE='n64 k64 4x4/2x4')
            args = [str(binary), '--algo', 'pearl-pow', '--pool', '127.0.0.1:1',
                '--wallet', 'prl1'+'q'*59, '--pearl-network', 'mainnet',
                '--pearl-target-semantics', 'base-before-penalty',
                '--pearl-transcript', str(Path(td)/'transcript'), '--batch', '256']
            # Actual CLI parses finite ranges before device admission.
            for value in ('0x8000000000000000:0x80000000ffffffff','0xffffffffffffffff:0xffffffffffffffff'):
                result=subprocess.run(args+['--pearl-work-range',value],env=baseline,capture_output=True,timeout=5)
                self.assertEqual(result.returncode,1)
                self.assertIn(b'shape experiment requires an owned bounded guard',result.stderr)
                self.assertNotIn(b'FORBIDDEN_DEVICE_SETUP',result.stdout+result.stderr)
            for extra in (['--pearl-work-range','bad'],
                          ['--pearl-work-range','0x0000000000000000:0xffffffffffffffff'],
                          ['--pearl-work-range','0x8000000000000000:0x80000000ffffffff']*2,
                          ['--pearl-work-range','0x8000000000000000:0x80000000ffffffff','--bench']):
                result=subprocess.run(args+extra,env=baseline,capture_output=True,timeout=5)
                self.assertEqual(result.returncode,1)
                self.assertIn(b'--pearl-work-range requires',result.stderr)
                self.assertNotIn(b'FORBIDDEN_DEVICE_SETUP',result.stdout+result.stderr)
            cases = [('exact', {})]
            for key in ('CANARY_PEARL_SHAPE_EXPERIMENT','CANARY_PEARL_SHAPE','CANARY_PEARL_TILE'):
                for value in (None, '', baseline[key]+' ', baseline[key]+'x'):
                    cases.append((key+repr(value),{key:value}))
            cases.extend([('prefix',{'CANARY_PEARL_TILE':'ptx k64'}),
                          ('legacy_shape',{'CANARY_PEARL_SHAPE':'4096x65536'}),
                          ('conflict',{'CANARY_PEARL_HARD_TARGET':'1'})])
            for name, changes in cases:
                with self.subTest(name=name):
                    env=baseline.copy()
                    for k,v in changes.items():
                        if v is None: env.pop(k,None)
                        else: env[k]=v
                    result=subprocess.run(args,env=env,capture_output=True,timeout=5)
                    output=result.stdout+result.stderr
                    self.assertEqual(result.returncode,1,output)
                    self.assertNotIn(b'FORBIDDEN_DEVICE_SETUP',output)
                    guard=b'shape experiment requires an owned bounded guard'
                    if name=='exact': self.assertIn(guard,output)
                    else: self.assertNotIn(guard,output)

if __name__ == '__main__': unittest.main()
