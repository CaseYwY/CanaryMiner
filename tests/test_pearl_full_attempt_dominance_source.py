"""Focused scope/diagnostic regression; native C++ test covers arithmetic."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
ALGO = (ROOT/'src/algos/pearl-pow/algo.cu').read_text()
STATS = (ROOT/'src/algos/pearl-pow/tuner_stats.h').read_text()

class ScopeTests(unittest.TestCase):
    def test_explicit_full_attempt_entry_only(self):
        self.assertEqual(ALGO.count('selectFullAttemptRateWinner(attemptRates)'), 1)
        self.assertIn('selectPairedRateWinner(quorateRates,', ALGO)
        helper = STATS.split('inline PairedRateSelection selectFullAttemptRateWinner(',1)[1].split('/** Project-owned generator',1)[0]
        self.assertNotIn('selectable', helper)
        self.assertNotIn('4096', helper)
        self.assertNotIn('65536', helper)
        self.assertIn('candidateSamples[i].begin() + 6', helper)
        self.assertIn('for (const auto &summary : result.summaries)', helper)

    def test_structured_diagnostics_and_full_matrix(self):
        for field in ('full-attempt disposition epoch=%zu m=%u n=%u',
                      'winner=%d stable=%d paired_median=%.17g spread=%.17g',
                      'observed_low=%.17g observed_high=%.17g reason=%s',
                      'confirmation_passed=%d'):
            self.assertIn(field, ALGO)
        self.assertIn('for (int i = 0; i < nCand; i++)', ALGO)

if __name__ == '__main__': unittest.main(verbosity=2)
