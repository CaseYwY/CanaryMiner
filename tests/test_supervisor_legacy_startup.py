"""Actual CPU-only supervisor traces must not promise retries after creation-time stop or original-epoch revocation."""
import subprocess
import sys
import unittest

PEER = None


class OriginalStartupIntent(unittest.TestCase):
    def setUp(self):
        if PEER is None:
            self.skipTest('requires an explicitly supplied CPU supervisor fixture')

    def trace(self, mode):
        result = subprocess.run([PEER, mode], capture_output=True, text=True, timeout=20)
        if result.returncode == 77:
            self.skipTest('native pidfd capability unavailable')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('0 failures', result.stdout)
        self.assertIn('stopping: draining every GPU', result.stdout)
        self.assertIn('stopped: 1 GPU(s)', result.stdout)
        self.assertNotIn('restart in', result.stdout)
        return result.stdout

    def test_created_cancel_is_stop_not_a_retry(self):
        self.assertNotIn('worker fault', self.trace('cancel-created'))

    def test_created_foreign_epoch_fault_is_not_a_retry(self):
        self.assertIn('explicit new session required', self.trace('fault-created'))


if __name__ == '__main__':
    PEER = sys.argv.pop(1) if len(sys.argv) > 1 else None
    unittest.main()
