"""CPU CLI-stub admission only; never starts real GPU work or a pool connection."""
import os
from pathlib import Path
import subprocess
import sys
import unittest

EXECUTABLE = None


class ThrottleCli(unittest.TestCase):
    def setUp(self):
        if EXECUTABLE is None:
            self.skipTest("Run directly with the built CLI host stub")

    def run_cli(self, *arguments, environment=None):
        return subprocess.run([str(EXECUTABLE), *arguments], env=environment, capture_output=True, timeout=8)

    def test_malformed_and_duplicate_values_are_usage_errors(self):
        for value in ("", "-1", "+1", "1.0", "1e2", "1001", "99999"):
            with self.subTest(value=value):
                result = self.run_cli("--work-wait-ms", value)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn(b"--work-wait-ms", result.stderr)
        self.assertEqual(self.run_cli("--work-wait-ms").returncode, 2)
        self.assertEqual(self.run_cli("--work-wait-ms", "0", "--work-wait-ms", "0").returncode, 2)

    def test_zero_help_and_benchmark_rejection(self):
        result = self.run_cli("--work-wait-ms", "0", "--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"not a duty cycle", result.stdout)
        result = self.run_cli("--work-wait-ms", "200", "--bench")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"ordinary Pearl pool mining", result.stderr)

    def test_research_selector_is_not_a_throttle_capability(self):
        for name in ("CANARY_PEARL_NATIVE_EVIDENCE", "CANARY_PEARL_ASYNC_SUBMIT", "CANARY_PEARL_INTEGRATED_PIPELINE"):
            environment = os.environ.copy()
            environment[name] = "1"
            result = self.run_cli("--work-wait-ms", "1", environment=environment)
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertIn(b"research/pipeline/evidence", result.stderr)


if __name__ == "__main__":
    EXECUTABLE = Path(sys.argv.pop(1)).resolve()
    unittest.main()
