"""Fixed CPU self-helper control through the actual native request mapping/pipe protocol, never invented arguments."""
import subprocess
import sys
import unittest

KIND = PEER = CONTROLLER = None


class BackgroundNativeParentGuard(unittest.TestCase):
    def setUp(self):
        if CONTROLLER is None:
            self.skipTest('requires explicitly supplied CPU native helper and parent-control executables')

    def test_helper_only_reply_through_actual_native_parent(self):
        result = subprocess.run([CONTROLLER, KIND, PEER], text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        label = 'IDENTITY' if KIND == 'game' else 'SAMPLING'
        self.assertEqual(result.stdout, 'SYNTHETIC ' + label + ' FIXTURE ONLY\n')

    def test_unknown_mode_refuses_without_parent_output(self):
        result = subprocess.run([PEER, '--unexpected-helper-mode'], text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 4)
        self.assertEqual(result.stdout, '')


if __name__ == '__main__':
    if len(sys.argv) > 3:
        KIND, PEER, CONTROLLER = sys.argv[1:4]
        del sys.argv[1:4]
        if KIND not in ('game', 'sampling'):
            raise SystemExit('expected game or sampling fixture kind')
    unittest.main()
