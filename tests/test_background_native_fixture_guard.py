"""An explicitly supplied CPU helper must reject unknown modes without constructing its parent owner."""
import subprocess
import sys
import unittest

KIND = PEER = None


class BackgroundFixtureDispatch(unittest.TestCase):
    def setUp(self):
        if PEER is None:
            self.skipTest('requires an explicitly supplied game identity or sampling CPU fixture')

    def test_helper_only_reply(self):
        result = subprocess.run([PEER, '--local-task-v1'], input='SYNTHETIC\n', text=True, capture_output=True, timeout=10)
        if result.returncode == 77:
            self.skipTest('native fixture capability unavailable')
        self.assertEqual(result.returncode, 0, result.stderr)
        label = 'IDENTITY' if KIND == 'game' else 'SAMPLING'
        self.assertEqual(result.stdout, 'SYNTHETIC ' + label + ' FIXTURE ONLY\n')

    def test_unknown_mode_refuses_without_parent_output(self):
        result = subprocess.run([PEER, '--unexpected-helper-mode'], text=True, capture_output=True, timeout=10)
        if result.returncode == 77:
            self.skipTest('native fixture capability unavailable')
        self.assertEqual(result.returncode, 4)
        self.assertEqual(result.stdout, '')


if __name__ == '__main__':
    if len(sys.argv) > 2:
        KIND, PEER = sys.argv[1:3]
        del sys.argv[1:3]
        if KIND not in ('game', 'sampling'):
            raise SystemExit('expected game or sampling fixture kind')
    unittest.main()
