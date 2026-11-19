#!/usr/bin/env python3
"""POSIX PTY integration for the actual host CLI: no CUDA, mining or external network.

Usage: python3 tests/test_settings_terminal.py PATH_TO_HOST_CLI
All runtime files are synthetic and stay in a private temporary directory.
"""
import json
import os
from pathlib import Path
import select
import signal
import sqlite3
import struct
import subprocess
import sys
import tempfile
import time


def main():
    if os.name != "posix":
        raise SystemExit("POSIX PTY test; use the native state/storage tests on Windows")
    import fcntl
    import pty
    import termios

    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="canary-ui-test-") as directory:
        root = Path(directory)
        config = root / "config.json"
        config.write_text(json.dumps({"schema_version": 1, "state_dir": str(root / "state")}), encoding="utf8")
        preferences = root / "state" / "ui-preferences.json"
        redirected = subprocess.run([executable, "settings", "--config", str(config)], capture_output=True, timeout=5)
        assert redirected.returncode == 2
        assert b"interactive terminal" in redirected.stderr
        assert not preferences.exists()
        checks = 3

        def session(edit, expected, supervisor=None, history_count=2, rows=24):
            nonlocal checks
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, 100, 0, 0))
            command = [executable, "settings", "--config", str(config)] if supervisor is None else [supervisor, "--dashboard-session-test", str(root / "state")]
            process = subprocess.Popen(command, stdin=slave, stdout=slave, stderr=slave,
                                       start_new_session=True)
            os.close(slave)
            transcript = b""

            def until(text, fresh=False):
                nonlocal transcript
                if fresh:
                    transcript = b""
                deadline = time.monotonic() + 8
                while text not in transcript:
                    if time.monotonic() > deadline:
                        raise AssertionError("terminal readiness deadline exceeded")
                    ready, _, _ = select.select([master], [], [], 0.1)
                    if ready:
                        try:
                            data = os.read(master, 65536)
                        except OSError:
                            raise AssertionError("terminal closed before expected state") from None
                        if not data:
                            raise AssertionError("unexpected terminal EOF")
                        transcript += data
                return transcript

            try:
                if supervisor:
                    until(b"[S]")  # label may wrap at the terminal boundary
                    until(f"all local {history_count}".encode())
                    os.write(master, b"s")
                until(b"CanaryMiner / Settings")
                assert b"no CUDA" not in transcript
                checks += 2
                edit(process, master, until)
                process.wait(timeout=5)
                assert process.returncode == 0
                checks += 1
                if expected is not None:
                    assert json.loads(preferences.read_text())["refresh_ms"] == expected
                    checks += 1
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
                os.close(master)

        def save(process, terminal, until):
            del process
            assert not preferences.exists()
            until(b"Donation link [ ]")
            os.write(terminal, b"\t \x1bOQ\r")
            until(b"Saved.")
            os.write(terminal, b"\x1b[B" * 3 + b" v")
            until(b"Show donation link: off -> on", fresh=True)
            assert "show_donation_link" not in json.loads(preferences.read_text())
            os.write(terminal, b"\r")
            until(b"Saved.", fresh=True)
            assert json.loads(preferences.read_text())["show_donation_link"] is True
            os.write(terminal, b"q")

        session(save, 1000)

        def cancel(process, terminal, until):
            del process
            until(b"1000 ms")
            until(b"Donation link [x]")
            os.write(terminal, b"\t \x1b")
            until(b"Unsaved changes")
            os.write(terminal, b"d")

        session(cancel, 1000)

        def stop(process, terminal, until):
            del until
            os.write(terminal, b"\t ")
            process.send_signal(signal.SIGINT)

        session(stop, 1000)

        def idle_disable(process, terminal, until):
            del process
            until(b"Donation link [x]")
            os.write(terminal, b"\t" + b"\x1b[B" * 3 + b" \x1bOQ\r")
            until(b"Saved.", fresh=True)
            assert "show_donation_link" not in json.loads(preferences.read_text())
            os.write(terminal, b"q")

        session(idle_disable, 1000)
        if len(sys.argv) > 2:
            donation_link = b"https://github.com/CaseYwY/CanaryMiner#developer-donation"
            def dashboard_edit(process, terminal, until):
                del process
                assert donation_link not in until(b"Donation link [ ]")
                os.write(terminal, b"\t v\r")
                until(b"Saved.")
                os.write(terminal, b"\x1b")
                until(b"[S]", fresh=True)
                os.write(terminal, b"s")
                until(b"CanaryMiner / Settings", fresh=True)
                os.write(terminal, b"\x1b[C" * 9 + b"\t " + b"\x1b[C" + b" v\r")
                until(b"Saved.", fresh=True)
                os.write(terminal, b"\x1b")
                main = until(b"[S]", fresh=True)
                assert b"[D]" not in main and b"[L]" not in main
                os.write(terminal, b"dl")
                main = until(b"[S]", fresh=True)
                assert b"WORKER OUTPUT" not in main and b"DETAILS" not in main
                os.write(terminal, b"s")
                until(b"CanaryMiner / Settings", fresh=True)
                # The previous visit confirmed the Log page. Display is the next group.
                os.write(terminal, b"\x1b[C\t" + b"\x1b[B" * 3 + b" v")
                until(b"Show donation link: off -> on")
                assert donation_link not in until(b"Show donation link: off -> on")
                os.write(terminal, b"\r")
                until(b"Saved.", fresh=True)
                assert json.loads(preferences.read_text())["show_donation_link"] is True
                os.write(terminal, b"\x1b")
                until(donation_link, fresh=True)
                os.write(terminal, b"q")

            session(dashboard_edit, 100, str(Path(sys.argv[2]).resolve()), rows=56)
            def dashboard_restart(process, terminal, until):
                del process
                until(b"Donation link [x]")
                os.write(terminal, b"\t" + b"\x1b[B" * 3 + b" v\r")
                until(b"Saved.", fresh=True)
                os.write(terminal, b"\x1b")
                assert donation_link not in until(b"[S]", fresh=True)
                assert "show_donation_link" not in json.loads(preferences.read_text())
                os.write(terminal, b"q")

            session(dashboard_restart, 100, str(Path(sys.argv[2]).resolve()), history_count=4, rows=56)
            with sqlite3.connect(root / "state" / "accepted-history.sqlite3") as database:
                assert database.execute("SELECT total FROM meta WHERE id=1").fetchone()[0] == 4
                assert database.execute("SELECT COUNT(*) FROM sessions WHERE ended IS NULL").fetchone()[0] == 0
                assert database.execute("SELECT gaps FROM meta WHERE id=1").fetchone()[0] == 1
        assert set(json.loads(preferences.read_text())) == {
            "schema_version", "refresh_ms", "show_events", "hide_worker", "color", "details_enabled", "log_enabled",
            "details_identity", "details_pool", "details_restarts", "details_errors", "log_severity", "log_unclassified",
            "log_rows", "log_timestamps", "log_iso_time", "log_follow"}
        assert json.loads(preferences.read_text())["schema_version"] == 2
        checks += 1
        print("settings_terminal: redirected rejection and all requested PTY scenarios passed")


if __name__ == "__main__":
    main()
