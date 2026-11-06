"""Externally supplied self-helper dispatch must never enter an owner parent recursively."""
import subprocess
import sys
import unittest

PEER = None


class EnergyFixtureDispatch(unittest.TestCase):
    def setUp(self):
        if PEER is None:
            self.skipTest('requires an explicitly supplied CPU energy fixture')

    def test_helper_only_reply(self):
        result = subprocess.run([PEER, '--local-task-v1'], input='SYNTHETIC\n', text=True, capture_output=True, timeout=10)
        if result.returncode == 77:
            self.skipTest('native fixture capability unavailable')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, 'SYNTHETIC ENERGY FIXTURE ONLY\n')

    def test_unknown_parent_mode_refuses_without_output(self):
        result = subprocess.run([PEER, '--unexpected-helper-mode'], text=True, capture_output=True, timeout=10)
        if result.returncode == 77:
            self.skipTest('native fixture capability unavailable')
        self.assertEqual(result.returncode, 4)
        self.assertEqual(result.stdout, '')


if __name__ == '__main__':
    PEER = sys.argv.pop(1) if len(sys.argv) > 1 else None
    unittest.main()
