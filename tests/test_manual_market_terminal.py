"""Actual POSIX native settings/supervisor PTY, synthetic offline prices and inert workers only."""
import json
import os
from pathlib import Path
import select
import struct
import subprocess
import sys
import tempfile
import time


def main():
    if os.name != "posix":
        raise SystemExit("POSIX PTY only; Windows uses native state/store/renderer tests")
    import fcntl
    import pty
    import termios
    cli, supervisor = [str(Path(p).resolve()) for p in sys.argv[1:3]]
    checks = 0
    with tempfile.TemporaryDirectory(prefix="canary-manual-market-") as directory:
        root = Path(directory)
        state = root / "state"
        config = root / "config.json"
        config.write_text(json.dumps({"schema_version": 1, "state_dir": str(state)}), encoding="utf8")
        file = state / "manual-market-preferences.json"
        ui_file = state / "ui-preferences.json"
        runtime_file = state / "runtime-preferences.json"
        count = state / "fixture-worker-starts.txt"
        redirected = subprocess.run([cli, "settings", "--no-market", "--config", str(config)], capture_output=True, timeout=5)
        assert redirected.returncode == 2 and not file.exists()
        checks += 1

        def session(command, work):
            nonlocal checks
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 60, 120, 0, 0))
            child = subprocess.Popen(command, stdin=slave, stdout=slave, stderr=slave, start_new_session=True)
            os.close(slave)
            transcript = b""
            market_opened = False

            def until(needle, fresh=False):
                nonlocal transcript
                if fresh:
                    transcript = b""
                deadline = time.monotonic() + 8
                while needle not in transcript:
                    if time.monotonic() > deadline:
                        raise AssertionError("native manual-market terminal readiness deadline")
                    readable, _, _ = select.select([master], [], [], 0.1)
                    if readable:
                        try:
                            data = os.read(master, 65536)
                        except OSError:
                            raise AssertionError("terminal closed before expected manual-market state") from None
                        if not data:
                            raise AssertionError("terminal EOF before expected manual-market state")
                        transcript += data
                return transcript

            def send(data):
                os.write(master, data)

            def open_market():
                nonlocal market_opened
                send((b"" if market_opened else b"\x1b[C" * 3) + b"\t\r")
                until(b"Manual/offline market", True)
                market_opened = True  # parent settings intentionally retains its last selected group

            try:
                work(send, until, open_market)
                child.wait(timeout=5)
                assert child.returncode == 0 and b"FORBIDDEN_DEVICE_SETUP" not in transcript
                checks += 1
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait(timeout=5)
                os.close(master)

        def setup(send, until, _):
            nonlocal checks
            until(b"CanaryMiner / Settings")
            # Keep a separate unsaved UI draft while reviewing/saving the offline file.
            send(b"\t " + b"\x1b[C" * 3 + b"\r")
            until(b"Manual/offline market", True)
            send(b" \t\rq\r")
            until(b"positive decimal", True)
            send(b"\x1b")
            until(b"> PRL price (USDT/PRL)", True)  # wait for resolved lone-Esc/browse, not the shared editor heading
            send(b"\r2.5000\r\tn\t \t\r0.8\r\tn\t\r60\r")
            until(b"Validity seconds: 60", True)
            send(b"v")
            until(b"Review manual-market-preferences.json", True)
            assert not file.exists() and not ui_file.exists() and not runtime_file.exists()
            send(b"\r")
            until(b"Saved.", True)
            saved = json.loads(file.read_text())
            assert saved["enabled"] and saved["fx_enabled"] and saved["prl_usdt"] == "2.5" and saved["usdt_usd"] == "0.8"
            assert 0 < saved["prl_as_of_utc"] <= saved["confirmed_utc"] <= int(time.time())
            assert saved["fx_as_of_utc"] > 0 and saved["validity_seconds"] == 60
            assert not count.exists() and not ui_file.exists() and not runtime_file.exists()
            checks += 4
            send(b"\x1b")
            until(b"CanaryMiner / Settings", True)
            send(b"\x1b")
            until(b"Unsaved changes", True)
            send(b"d")

        session([cli, "settings", "--config", str(config)], setup)
        original = json.loads(file.read_text())

        def live(send, until, open_market):
            nonlocal checks
            until(b"MANUAL/OFFLINE PRICE REFERENCES")
            displayed = until(b"2.00000000 USD/PRL")
            until(b"all local 2")  # child has started and its synthetic stats/history observation is ready
            assert b"1h equal-time mean: N/A" in until(b"1h equal-time mean: N/A")
            assert b"PRODUCTION AND REFERENCE VALUATION" not in displayed and b"Estimated net" not in displayed
            starts_before = count.read_text().splitlines()
            assert starts_before == ["start"]
            send(b"s")
            until(b"CanaryMiner / Settings", True)
            open_market()
            # Save a changed validity without touching either reference timestamp.
            send(b"\t" * 6 + b"\r120\rv\r")
            until(b"Saved.", True)
            saved = json.loads(file.read_text())
            assert saved["validity_seconds"] == 120
            assert saved["prl_as_of_utc"] == original["prl_as_of_utc"] and saved["fx_as_of_utc"] == original["fx_as_of_utc"]
            send(b"\x1b")
            until(b"CanaryMiner / Settings", True)
            send(b"\x1b")
            until(b"MANUAL/OFFLINE PRICE REFERENCES", True)
            assert count.read_text().splitlines() == starts_before
            checks += 5
            # Turn the reference display off live, not mining or history collection.
            send(b"s")
            until(b"CanaryMiner / Settings", True)
            open_market()
            send(b" v\r")
            until(b"Saved.", True)
            send(b"\x1b")
            until(b"CanaryMiner / Settings", True)
            send(b"\x1b")
            main = until(b"[S]", True)
            assert b"MANUAL/OFFLINE" not in main and not json.loads(file.read_text())["enabled"]
            assert count.read_text().splitlines() == starts_before
            checks += 2
            send(b"q")

        session([supervisor, "--dashboard-session-test", str(state)], live)
        # A persisted enabled source is suppressed by the one-session CLI override, not rewritten.
        saved = json.loads(file.read_text())
        saved["enabled"] = True
        saved["prl_as_of_utc"] = saved["fx_as_of_utc"] = saved["confirmed_utc"] = int(time.time())
        file.write_text(json.dumps(saved), encoding="utf8")

        def overridden(send, until, open_market):
            nonlocal checks
            main = until(b"[S]")
            assert b"MANUAL/OFFLINE" not in main
            send(b"s")
            until(b"CanaryMiner / Settings", True)
            open_market()
            until(b"--no-market", True)
            send(b"v\r")
            until(b"Saved; --no-market", True)
            assert json.loads(file.read_text())["enabled"]
            checks += 2
            send(b"q")

        session([supervisor, "--manual-no-market-session-test", str(state)], overridden)
        assert not ui_file.exists() and not runtime_file.exists()
        assert len(count.read_text().splitlines()) == 2
        assert not list(state.glob("*credit*"))
        checks += 3
    print(f"manual-market PTY: {checks} checks; idle/save/restart-free live display/CLI override passed")


if __name__ == "__main__":
    main()
