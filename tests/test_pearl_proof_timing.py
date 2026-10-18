"""Actual CPU timing state machine and real fake-transport source off/on equivalence."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PREFIX = '[pearl-proof-timing] '


class ProofTimingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('g++')
        if not compiler:
            raise RuntimeError('CPU g++ required; missing compiler is not a passing skip')
        cls.stage = tempfile.TemporaryDirectory(prefix='canary-proof-timing-')
        cls.addClassCleanup(cls.stage.cleanup)
        cls.binaries = {}
        for name in ('test_pearl_proof_timing', 'test_pearl_pool_source', 'test_pearl_run_session'):
            path = Path(cls.stage.name) / name
            command = [compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                       '-Wno-error=reorder', '-Wno-error=unused-variable', '-pthread', '-fsanitize=undefined',
                       '-fno-sanitize-recover=all', 'tests/' + name + '.cpp', '-o', str(path)]
            result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=120)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            cls.binaries[name] = path

    def run_binary(self, name, option=None, args=()):
        env = dict(os.environ)
        env.pop('CANARY_PEARL_PROOF_TIMING', None)
        if option is not None:
            env['CANARY_PEARL_PROOF_TIMING'] = option
        result = subprocess.run([str(self.binaries[name]), *args], cwd=ROOT, env=env,
                                text=True, capture_output=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def records(self, stderr):
        records = []
        for line in stderr.splitlines():
            if line.startswith(PREFIX):
                record = dict(item.split('=', 1) for item in line[len(PREFIX):].split())
                self.assertEqual(record['v'], '1')
                self.assertIn(record['status'], ('complete', 'partial', 'invalid'))
                self.assertRegex(record['nonce'], r'^[0-9a-f]{16}$')
                for key, value in record.items():
                    if key not in ('scope', 'status', 'nonce'):
                        self.assertRegex(value, r'^\d+$')
                records.append(record)
        return records

    def test_actual_cpu_state_machine(self):
        result = self.run_binary('test_pearl_proof_timing')
        self.assertIn('PASS: proof and network timing', result.stdout)
        self.assertEqual(result.stderr, '')

    def test_default_off_and_malformed_are_silent(self):
        for option in (None, '', '0', '01', 'true', '1 ', ' 1', '11'):
            with self.subTest(option=option):
                result = self.run_binary('test_pearl_proof_timing', option, ('--emit',))
                self.assertEqual(result.stdout, 'unchanged ordinary output\n')
                self.assertEqual(result.stderr, '')

    def test_opt_in_emits_bounded_separate_diagnostics(self):
        result = self.run_binary('test_pearl_proof_timing', '1', ('--emit',))
        self.assertEqual(result.stdout, 'unchanged ordinary output\n')
        native, source, host = self.records(result.stderr)
        for record in (native, source, host):
            self.assertEqual(record['status'], 'complete')
            self.assertEqual(record['proof_token'], '7')
            stages = sum(int(v) for k, v in record.items() if k.endswith('_ns') and
                         k not in ('total_ns', 'start_ns', 'end_ns'))
            self.assertEqual(stages, int(record['total_ns']))
            self.assertEqual(int(record['end_ns']) - int(record['start_ns']), int(record['total_ns']))
        self.assertEqual(native['scope'], 'native')
        self.assertEqual(native['stages'], '6')
        self.assertEqual((native['m'], native['n'], native['k'], native['rank']), ('4096', '65536', '2048', '128'))
        self.assertEqual((native['a_bytes'], native['bt_bytes']), ('8388608', '134217728'))
        self.assertEqual(source['scope'], 'source-presubmit')
        self.assertEqual(source['stages'], '2')
        self.assertEqual(host['scope'], 'host-verify')
        self.assertEqual(host['stages'], '1')

    def test_real_pool_source_acceptance_stale_and_wire_invariants_unchanged(self):
        off = self.run_binary('test_pearl_pool_source')
        on = self.run_binary('test_pearl_pool_source', '1')
        self.assertEqual(off.stdout, on.stdout)
        self.assertIn('Pearl pool source observability: ok', on.stdout)
        self.assertIn('bounded pre-submit intake and local-only retirement:', on.stdout)
        self.assertEqual(self.records(off.stderr), [])
        self.assertEqual(off.stderr, ''.join(line + '\n' for line in on.stderr.splitlines() if not line.startswith((PREFIX, '[pearl-proof-binding] '))))
        records = self.records(on.stderr)
        self.assertTrue(records)
        bindings=[dict(token.split('=',1) for token in line.split()[1:]) for line in on.stderr.splitlines()
                  if line.startswith('[pearl-proof-binding] ')]
        self.assertTrue(bindings)
        for binding in bindings:
            self.assertEqual(set(binding),{'v','nonce','proof_token','request_id','plain_proof_b3','observed_ns'})
            self.assertRegex(binding['nonce'],r'^[0-9a-f]{16}$')
            self.assertRegex(binding['plain_proof_b3'],r'^[0-9a-f]{64}$')
            self.assertGreater(int(binding['observed_ns']),0)
        self.assertTrue(any(r['status'] == 'complete' for r in records))
        self.assertTrue(any(r['status'] == 'partial' for r in records))
        self.assertTrue(all(r['scope'] in ('source-presubmit', 'source-network') and r['status'] != 'invalid' for r in records))

    def test_real_run_session_verification_outcomes_unchanged(self):
        off = self.run_binary('test_pearl_run_session')
        on = self.run_binary('test_pearl_run_session', '1')
        self.assertEqual(off.stdout, on.stdout)
        self.assertEqual(off.stderr, ''.join(line + '\n' for line in on.stderr.splitlines() if not line.startswith((PREFIX, '[pearl-proof-binding] '))))
        records = [r for r in self.records(on.stderr) if r['scope'] == 'host-verify']
        self.assertTrue(records)
        self.assertTrue(any(r['status'] == 'complete' for r in records))
        self.assertTrue(all(r['status'] != 'invalid' for r in records))
        for r in records:
            self.assertEqual(int(r['verify_ns']) + int(r['tail_ns']), int(r['total_ns']))
            self.assertEqual(int(r['end_ns']) - int(r['start_ns']), int(r['total_ns']))

    def test_native_hooks_enclose_existing_stages_without_new_device_waits(self):
        text = (ROOT / 'src/algos/pearl-pow/algo.cu').read_text()
        body = text.split('    bool openWin(', 1)[1].split('    bool recheck(', 1)[0]
        self.assertEqual(body.count('timing.boundary();'), 6)
        self.assertEqual(body.count('timing.completeOnExit();'), 1)
        self.assertNotIn('timing.finish()', body)
        self.assertLess(body.index('proof_timing::NativeScope'), body.index('std::vector<uint32_t> index'))
        self.assertLess(body.index('timing.proofToken(sol.pearlProofToken)'), body.index('out->push_back(std::move(sol))'))
        # The only CUDA operations remain the ten pre-existing synchronous copies
        # (including debug transcript readback), not new waits/events/streams.
        self.assertEqual(re.findall(r'\b(cuda\w+)\s*\(', body), ['cudaMemcpy'] * 6 +
                         ['cudaGetErrorString', 'cudaMemcpy', 'cudaMemcpy', 'cudaMemcpy', 'cudaMemcpy'])
        # (the fourth trailing copy is the retained-transcript association check of the SM120 TMA scan mode; it only
        # executes when that mode is active and was SM120-lineage-only before the host paths were unified)
        pool = (ROOT / 'src/core/pearl_pool.h').read_text().split('    bool submit(const Job &job', 1)[1]
        self.assertLess(pool.index('timing.finish();'), pool.index('sendLineUntil(body, operationDeadline)'))


if __name__ == '__main__':
    unittest.main()
