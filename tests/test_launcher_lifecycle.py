"""Execute copied launchers with an inert native fixture. Never run a real miner or change registry state."""
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
STUB = None


class LauncherLifecycle(unittest.TestCase):
    def setUp(self):
        if STUB is None:
            self.skipTest("Run directly with the built test_product_intent executable")
        self.temporary = tempfile.TemporaryDirectory(prefix="canary-launcher-fixture-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        import shutil
        shutil.copy2(STUB, self.directory / ("canary-miner.exe" if os.name == "nt" else "canary-miner"))

    def command(self, status, fast=True, maximum="3"):
        name = "start-advanced.bat" if os.name == "nt" else "start-advanced.sh"
        text = (ROOT / "launchers" / name).read_text(encoding="utf8")
        text = text.replace('set "WALLET=PUT_YOUR_PRL_ADDRESS_HERE"', 'set "WALLET=prl1-example-fixture"')
        text = text.replace('WALLET="PUT_YOUR_PRL_ADDRESS_HERE"', 'WALLET="prl1-example-fixture"')
        text = text.replace('set "EXTRA="', f'set "EXTRA=--launcher-fixture {status}"')
        text = text.replace('EXTRA=""', f'EXTRA="--launcher-fixture {status}"')
        text = text.replace('set "MAX_RESTARTS=3"', f'set "MAX_RESTARTS={maximum}"')
        text = text.replace('MAX_RESTARTS=3', f'MAX_RESTARTS={maximum}')
        # Test copies must not touch HKCU, wait interactively or incur artificial five-second delays.
        text = "\n".join(line for line in text.splitlines() if not line.lstrip().lower().startswith("reg add ")) + "\n"
        if fast:
            text = text.replace("timeout /t 5 /nobreak >nul", "cmd /d /c exit 0")
            text = text.replace("sleep 5", "sleep 0")
        script = self.directory / name
        script.write_text(text, encoding="utf8", newline="\r\n" if os.name == "nt" else "\n")
        return [os.environ.get("COMSPEC", "cmd.exe"), "/d", "/c", str(script)] if os.name == "nt" else ["sh", str(script)]

    def test_only_runtime_errors_retry_and_budget_is_bounded(self):
        for status in (0, 1, 2, 3, 4, 5, 6, 7, 8, 130, 139, 143):
            with self.subTest(status=status):
                result = subprocess.run(self.command(status), cwd=self.directory, capture_output=True, timeout=8)
                self.assertEqual(result.returncode, status, result.stdout + result.stderr)
                self.assertEqual(result.stdout.count(b"fixture-child-started"), 4 if status == 1 else 1)

    def test_invalid_or_zero_retry_budget(self):
        for maximum in ("", "-1", "100", "many", "0"):
            with self.subTest(maximum=maximum):
                result = subprocess.run(self.command(1, maximum=maximum), cwd=self.directory, capture_output=True, timeout=5)
                self.assertEqual(result.returncode, 1 if maximum == "0" else 2, result.stdout + result.stderr)
                self.assertEqual(result.stdout.count(b"fixture-child-started"), 1 if maximum == "0" else 0)

    @unittest.skipIf(os.name == "nt", "POSIX signal forwarding; Windows console event qualification is separate")
    def test_termination_during_child_or_backoff_never_restarts(self):
        for status in ("wait", 1):
            with self.subTest(status=status):
                process = subprocess.Popen(self.command(status, fast=False), cwd=self.directory, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                transcript = b""
                try:
                    deadline = time.monotonic() + 5
                    marker = b"fixture-child-started" if status == "wait" else b"in 5 seconds"
                    while marker not in transcript:
                        if time.monotonic() > deadline:
                            self.fail("fixture readiness deadline exceeded")
                        ready, _, _ = select.select([process.stdout], [], [], 0.1)
                        if ready:
                            data = os.read(process.stdout.fileno(), 65536)
                            self.assertTrue(data, transcript)
                            transcript += data
                    process.send_signal(signal.SIGTERM)
                    stdout, stderr = process.communicate(timeout=7)
                    transcript += stdout
                    self.assertEqual(process.returncode, 143, transcript + stderr)
                    self.assertEqual(transcript.count(b"fixture-child-started"), 1)
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.communicate(timeout=3)


if __name__ == "__main__":
    STUB = Path(sys.argv.pop(1)).resolve()
    unittest.main()
