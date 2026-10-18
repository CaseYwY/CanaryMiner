"""CPU-only source-boundary regression; C++ policy tests carry arithmetic checks."""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
SOURCE=(ROOT/'src/algos/pearl-pow/algo.cu').read_text()

class FullAttemptSourceTests(unittest.TestCase):
    def test_complete_matrix_is_new_inside_each_epoch(self):
        loop=SOURCE.split('for (size_t epoch = 0;',1)[1].split('if (!selection.valid)',1)[0]
        self.assertLess(loop.index('std::vector<std::vector<double>> attemptRates(nCand)'),loop.index('for (size_t round'))
        self.assertLess(loop.index('attemptRates[idx].push_back(rate)'),loop.index('selectFullAttemptRateWinner(attemptRates)'))
        self.assertIn('if (selection.valid) break;',loop)
        self.assertIn('publication_.poisoned() ||',loop)
        self.assertEqual(loop.count('measureAttemptSample('),1)

    def test_measurement_failure_returns_before_selection_or_retry(self):
        loop=SOURCE.split('for (size_t epoch = 0;',1)[1].split('if (!selection.valid)',1)[0]
        failure=loop.split('if (!measureAttemptSample',1)[1].split('attemptRates[idx].push_back(rate)',1)[0]
        self.assertIn('return false;',failure)
        self.assertNotIn('retryFullAttemptEpoch',failure)

    def test_raw_measurement_identifies_every_epoch_and_round(self):
        body=SOURCE.split('bool measureAttemptSample(',1)[1].split('bool allocate()',1)[0]
        self.assertIn('full-attempt sample epoch=%zu round=%zu',body)
        self.assertIn('candidates=%llu elapsed_ms=%.9g',body)
        self.assertIn('rate_cps=%.17g valid=%d',body)
        self.assertEqual(body.count('runAttempt(nonce)'),1)
        self.assertIn('fullAttemptNonce(s.m, s.n, epoch, round, 0, &warmupNonce)',body)
        self.assertIn('!completeAttempt(warmupNonce)',body)
        self.assertIn('s.m, s.n, epoch, round, completeAttempt, &completed',body)
        self.assertIn('fullAttemptBatchRate(tiles(), completed, elapsedMs)',body)
        self.assertIn('attempts=%llu completed=%llu tiles=%llu',body)

    def test_every_attempt_keeps_production_drain(self):
        body=SOURCE.split('auto completeAttempt =',1)[1].split('std::uint64_t warmupNonce',1)[0]
        self.assertLess(body.index('runAttempt(nonce)'),body.index('cudaMemcpyAsync'))
        self.assertLess(body.index('cudaMemcpyAsync'),body.index('synchronizeStreamStatus()'))
        self.assertLess(body.index('synchronizeStreamStatus()'),body.index('hitReadback_ != 0'))
        self.assertIn('publication_.poison();',body)
        self.assertIn('return false;',body)

    def test_record_follows_cleanup_and_failure_poison(self):
        body=SOURCE.split('bool measureAttemptSample(',1)[1].split('bool allocate()',1)[0]
        self.assertLess(body.rindex('release();'),body.index('recordSample(ok && rate.valid'))
        self.assertIn('recordSample(ok && rate.valid && !publication_.poisoned())',body)
        self.assertIn('valid ? rate.candidatesPerSecond : 0.0',body)
        self.assertNotIn('noncePrefix',body)

if __name__=='__main__': unittest.main(verbosity=2)
