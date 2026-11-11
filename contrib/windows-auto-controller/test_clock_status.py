"""Exercise the installed heartbeat code using temporary files and Windows PowerShell.

The tests import the controller without calling its entry point and extract only
the helper's Write-Status function. They never launch mining or GPU commands.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock


sys.dont_write_bytecode = True
CONTROLLER = None
WRITE_STATUS = None
POWERSHELL = Path(os.environ["SystemRoot"], "System32", "WindowsPowerShell",
                  "v1.0", "powershell.exe")


class ClockStatusTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="canary-heartbeat-test-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.status = self.directory / "clock-status.json"
        self.lease = object.__new__(CONTROLLER.ClockLease)
        self.lease.directory = self.directory
        self.lease.session = "fixture-session"
        self.write_ack()

    def write_ack(self, **changes):
        value = dict(session=self.lease.session, success=True, updated_at=time.time())
        value.update(changes)
        self.status.write_text(json.dumps(value), encoding="utf-8")
        return value

    def powershell_script(self, body):
        script = self.directory / "fixture.ps1"
        preamble = """param([string]$Target, [string]$Gate, [string]$Ready)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
Add-Type -AssemblyName Microsoft.VisualBasic
$statusFile = $Target
$session = 'fixture-session'
$lastOutput = ''
"""
        script.write_text(preamble + WRITE_STATUS + "\n" + body, encoding="utf-8-sig")
        return [str(POWERSHELL), "-NoLogo", "-NoProfile", "-NonInteractive",
                "-ExecutionPolicy", "Bypass", "-File", str(script),
                "-Target", str(self.status), "-Gate", str(self.directory / "gate"),
                "-Ready", str(self.directory / "ready")]

    def test_transient_open_failures_recover(self):
        for errors in ([FileNotFoundError("fixture")], [PermissionError("fixture")],
                       [FileNotFoundError("fixture"), PermissionError("fixture")]):
            with self.subTest(errors=[type(error).__name__ for error in errors]):
                with mock.patch.object(CONTROLLER, "read_json",
                                       side_effect=errors + [self.write_ack()]) as read:
                    self.lease.check()
                self.assertEqual(read.call_count, len(errors) + 1)

    def test_persistent_open_failures_are_bounded(self):
        for error in (FileNotFoundError("fixture"), PermissionError("fixture")):
            with self.subTest(error=type(error).__name__):
                started = time.monotonic()
                with mock.patch.object(CONTROLLER, "read_json", side_effect=error) as read:
                    with self.assertRaises(type(error)):
                        self.lease.check()
                elapsed = time.monotonic() - started
                self.assertGreaterEqual(elapsed, 0.45)
                self.assertLess(elapsed, 1.5)
                self.assertGreater(read.call_count, 1)

    def test_malformed_json_fails_without_retry(self):
        self.status.write_text("{broken", encoding="utf-8")
        with mock.patch.object(CONTROLLER, "read_json", wraps=CONTROLLER.read_json) as read:
            with self.assertRaises(json.JSONDecodeError):
                self.lease.check()
        self.assertEqual(read.call_count, 1)

    def test_invalid_heartbeat_fails_without_retry(self):
        for changes in (dict(session="other"), dict(success=False),
                        dict(updated_at=time.time() - 60),
                        dict(updated_at=time.time() + 60)):
            with self.subTest(changes=changes):
                value = self.write_ack(**changes)
                with mock.patch.object(CONTROLLER, "read_json", return_value=value) as read:
                    with self.assertRaisesRegex(RuntimeError, "heartbeat failed"):
                        self.lease.check()
                self.assertEqual(read.call_count, 1)

    def test_actual_helper_concurrent_500_writes(self):
        command = self.powershell_script("""
[IO.File]::WriteAllText($Ready, 'ready')
$timer = [Diagnostics.Stopwatch]::StartNew()
while (-not (Test-Path -LiteralPath $Gate)) {
    if ($timer.Elapsed.TotalSeconds -gt 10) { throw 'Reader did not start.' }
    Start-Sleep -Milliseconds 5
}
for ($i = 0; $i -lt 500; $i++) { Write-Status 'unlock' $true '' }
Write-Output 'writes=500'
""")
        failures = []
        checks = 0
        with tempfile.TemporaryFile() as output, tempfile.TemporaryFile() as errors:
            process = subprocess.Popen(command, stdout=output, stderr=errors,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                deadline = time.monotonic() + 20
                ready = self.directory / "ready"
                while not ready.exists() and process.poll() is None:
                    if time.monotonic() >= deadline:
                        self.fail("PowerShell fixture did not become ready.")
                    time.sleep(0.01)
                self.assertTrue(ready.exists(), "PowerShell fixture exited before ready.")
                (self.directory / "gate").write_text("go", encoding="ascii")
                while process.poll() is None:
                    if time.monotonic() >= deadline:
                        self.fail("Concurrent heartbeat fixture exceeded 20 seconds.")
                    try:
                        self.lease.check()
                    except (OSError, ValueError, RuntimeError) as error:
                        failures.append(f"{type(error).__name__}: {error}")
                    checks += 1
                    time.sleep(0.0005)
                output.seek(0)
                errors.seek(0)
                stdout = output.read().decode("utf-8", "replace")
                stderr = errors.read().decode("utf-8", "replace")
                self.assertEqual(process.returncode, 0, stderr[-1500:])
                self.assertIn("writes=500", stdout)
                self.assertGreater(checks, 5)
                self.assertEqual(failures, [], str(failures[:5]))
                self.lease.check()
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)

    def test_helper_permanent_sharing_lock_is_bounded(self):
        command = self.powershell_script("""
$timer = [Diagnostics.Stopwatch]::StartNew()
try {
    Write-Status 'unlock' $true ''
    throw 'Write unexpectedly succeeded through the permanent sharing lock.'
} catch {
    if ($_.Exception.Message -like 'Write unexpectedly*') { throw }
    @{ elapsed_ms = $timer.Elapsed.TotalMilliseconds; error = $_.Exception.Message } |
        ConvertTo-Json -Compress
}
""")
        with self.status.open("rb"):
            result = subprocess.run(command, capture_output=True, timeout=10,
                                    creationflags=subprocess.CREATE_NO_WINDOW)
        self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", "replace"))
        value = json.loads(result.stdout.decode("utf-8-sig"))
        self.assertGreaterEqual(value["elapsed_ms"], 450, value)
        self.assertLess(value["elapsed_ms"], 1500, value)
        self.assertTrue(self.status.exists())
        self.lease.check()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-dir", required=True, type=Path)
    arguments, unittest_arguments = parser.parse_known_args()
    install = arguments.install_dir.resolve(strict=True)
    spec = importlib.util.spec_from_file_location("canary_heartbeat_test_controller",
                                                  install / "auto_controller.py")
    global CONTROLLER, WRITE_STATUS
    CONTROLLER = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(CONTROLLER)
    helper = (install / "gpu-clock-helper.ps1").read_text(encoding="utf-8-sig")
    match = re.search(r"(?ms)^function Write-Status\b.*?^\}", helper)
    if match is None:
        parser.error("gpu-clock-helper.ps1 has no standalone Write-Status function")
    WRITE_STATUS = match.group()
    unittest.main(argv=[sys.argv[0], *unittest_arguments])


if __name__ == "__main__":
    main()
