#!/usr/bin/env python3
"""POSIX PTY: actual settings controller plus fake supervised worker, no GPU/external network."""
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
        raise SystemExit("POSIX PTY only; use native Windows state/storage/CLI tests")
    import fcntl
    import pty
    import termios

    executable, supervisor = map(lambda value: str(Path(value).resolve()), sys.argv[1:3])
    with tempfile.TemporaryDirectory(prefix="canary-runtime-pty-") as temporary:
        root = Path(temporary)
        state = root / "state"
        config = root / "config.json"
        config.write_text(json.dumps({"schema_version": 1, "state_dir": str(state)}), encoding="utf8")
        preferences = state / "runtime-preferences.json"
        ui_preferences = state / "ui-preferences.json"
        database = state / "accepted-history.sqlite3"

        class Session:
            def __init__(self, mining=False):
                self.master, slave = pty.openpty()
                fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 100, 0, 0))
                arguments = [supervisor, "--dashboard-session-test", str(state)] if mining else [executable, "settings", "--config", str(config)]
                self.process = subprocess.Popen(arguments, stdin=slave, stdout=slave, stderr=slave, start_new_session=True)
                os.close(slave)
                self.transcript = b""
            def __enter__(self):
                return self
            def __exit__(self, *_):
                if self.process.poll() is None:
                    self.process.kill()
                    self.process.wait(timeout=5)
                os.close(self.master)
            def write(self, text):
                os.write(self.master, text)
            def until(self, text, fresh=False):
                if fresh:
                    self.transcript = b""
                deadline = time.monotonic() + 10
                while text not in self.transcript:
                    if time.monotonic() > deadline:
                        raise AssertionError("runtime terminal readiness deadline exceeded")
                    ready, _, _ = select.select([self.master], [], [], 0.1)
                    if ready:
                        try:
                            chunk = os.read(self.master, 65536)
                        except OSError:
                            raise AssertionError("terminal closed before runtime state") from None
                        assert chunk, "terminal EOF before runtime state"
                        self.transcript += chunk
                return self.transcript
            def runtime(self):
                self.until(b"CanaryMiner / Settings")
                self.write(b"\x1b[C\t\r")
                self.until(b"CanaryMiner / Runtime controls", fresh=True)
            def back_to_main(self):
                self.write(b"\x1b")
                self.until(b"CanaryMiner / Settings", fresh=True)
                self.write(b"\x1b")
                return self.until(b"[S]", fresh=True)
            def finished(self):
                self.process.wait(timeout=7)
                assert self.process.returncode == 0

        with Session() as session:
            session.runtime()
            session.write(b"\rq\r")
            session.until(b"Enter an integer", fresh=True)
            assert session.process.poll() is None  # Q is input in the numeric editor
            session.write(b"\x1b")
            session.until(b"Configured host wait", fresh=True)
            session.write(b"\r321\r\t v")
            session.until(b"Review runtime-preferences.json only", fresh=True)
            assert not preferences.exists()
            session.write(b"\r")
            session.until(b"Saved. Next explicit start only", fresh=True)
            assert json.loads(preferences.read_text())["work_wait_ms"] == 321
            assert not json.loads(preferences.read_text())["history_enabled"]
            assert not ui_preferences.exists() and not database.exists()
            session.write(b"\x1b")
            session.until(b"CanaryMiner / Settings", fresh=True)
            session.write(b"q")
            session.finished()

        with Session() as session:
            session.runtime()
            session.write(b"\r222\r\x1b")
            session.until(b"Unsaved runtime defaults", fresh=True)
            session.write(b"d")
            session.until(b"CanaryMiner / Settings", fresh=True)
            session.write(b"q")
            session.finished()
            assert json.loads(preferences.read_text())["work_wait_ms"] == 321

        with Session() as session:
            session.runtime()
            session.write(b"\r400\rv")
            session.until(b"Review runtime-preferences.json only", fresh=True)
            preferences.write_text(json.dumps({"schema_version": 1, "work_wait_ms": 333, "history_enabled": True}), encoding="utf8")
            session.write(b"\r")
            session.until(b"changed externally", fresh=True)
            session.process.send_signal(signal.SIGINT)
            session.finished()
            assert json.loads(preferences.read_text())["work_wait_ms"] == 333
            assert not database.exists() and not ui_preferences.exists()

        with Session(mining=True) as session:
            session.until(b"all local 2")
            session.write(b"s")
            session.runtime()
            session.until(b"Current host wait: 0 ms (CLI override)")
            session.until(b"Current history: on")
            session.write(b"\r777\r\t v\r")
            session.until(b"Saved. Next explicit start only", fresh=True)
            session.until(b"Current history: on")
            assert not json.loads(preferences.read_text())["history_enabled"]
            assert b"all local 2" in session.back_to_main()
            session.write(b"q")
            session.finished()
        with sqlite3.connect(database) as db:
            assert db.execute("SELECT total FROM meta").fetchone()[0] == 2

        with Session(mining=True) as session:
            main_frame = session.until(b"[S]")
            assert b"ACCEPTED HISTORY" not in main_frame
            session.write(b"s")
            session.runtime()
            session.until(b"Current history: off")
            session.until(b"Configured host wait: 777")
            session.write(b"\t v\r")
            session.until(b"Saved. Next explicit start only", fresh=True)
            assert b"ACCEPTED HISTORY" not in session.back_to_main()
            session.write(b"q")
            session.finished()
        with sqlite3.connect(database) as db:
            assert db.execute("SELECT total FROM meta").fetchone()[0] == 2

        with Session(mining=True) as session:
            session.until(b"all local 4")
            session.write(b"q")
            session.finished()
        with sqlite3.connect(database) as db:
            assert db.execute("SELECT total FROM meta").fetchone()[0] == 4
        assert not ui_preferences.exists()
        print("runtime_settings_terminal: contextual input/save/discard/stale writers, immutable current session and next-start history passed")


if __name__ == "__main__":
    main()
