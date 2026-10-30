#!/usr/bin/env python3
"""Native Games settings in an actual POSIX PTY; synthetic CPU supervisor only."""
import json
import os
from pathlib import Path
import select
import signal
import struct
import subprocess
import sys
import tempfile
import time


def main():
    if os.name != "posix":
        raise SystemExit("POSIX PTY only; Windows uses native state/store/CLI tests")
    import fcntl
    import pty
    import termios

    executable, supervisor = [str(Path(s).resolve()) for s in sys.argv[1:3]]
    checks = 0
    with tempfile.TemporaryDirectory(prefix="canary-game-pty-") as temporary:
        root = Path(temporary)
        state = root / "state"
        config = root / "config.json"
        config.write_text(json.dumps({"schema_version": 1, "state_dir": str(state)}), encoding="utf8")
        preferences = state / "game-preferences.json"

        class Session:
            def __init__(self, running=False):
                self.master, slave = pty.openpty()
                fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
                args = [supervisor, "--dashboard-session-test", str(state)] if running else [executable, "settings", "--config", str(config)]
                self.process = subprocess.Popen(args, stdin=slave, stdout=slave, stderr=slave, start_new_session=True)
                os.close(slave)
                self.transcript = b""
                self.running = running
            def __enter__(self):
                if self.running:
                    self.until(b"[S]")
                    self.send(b"s")
                self.until(b"CanaryMiner / Settings")
                self.send(b"\x1b[C" * 5 + b"\t\r")
                self.until(b"Games / saved defaults")
                return self
            def __exit__(self, *_):
                if self.process.poll() is None:
                    self.process.kill()
                    self.process.wait(timeout=5)
                os.close(self.master)
            def send(self, text):
                self.transcript = b""
                os.write(self.master, text)
            def until(self, text):
                deadline = time.monotonic() + 8
                while text not in self.transcript:
                    if time.monotonic() >= deadline:
                        raise AssertionError("game settings terminal readiness deadline exceeded")
                    ready, _, _ = select.select([self.master], [], [], 0.1)
                    if ready:
                        try:
                            chunk = os.read(self.master, 65536)
                        except OSError:
                            raise AssertionError("game settings terminal closed early") from None
                        assert chunk, "game settings terminal EOF"
                        self.transcript += chunk
                return self.transcript
            def finished(self):
                self.process.wait(timeout=7)
                assert self.process.returncode == 0

        with Session() as s:
            s.send(b" \t\rQDLS.exe\rv")
            s.until(b"Review game-preferences.json only")
            assert not preferences.exists() and s.process.poll() is None
            checks += 2
            s.send(b"\r")
            s.until(b"Saved game defaults")
            value = json.loads(preferences.read_text())
            assert value["mode"] == "pause" and value["target_executable"] == "QDLS.exe"
            assert not (state / "runtime-preferences.json").exists() and not (state / "ui-preferences.json").exists()
            assert not (state / "accepted-history.sqlite3").exists()
            checks += 3
            s.send(b"q")
            s.finished()
        original = preferences.read_bytes()
        with Session() as s:
            s.send(b"\t\rdiscard-fixture.exe\r\x1b")
            s.until(b"Unsaved game defaults")
            s.send(b"d")
            s.until(b"CanaryMiner / Settings")
            s.send(b"q")
            s.finished()
            assert preferences.read_bytes() == original
            checks += 1
        with Session() as s:
            s.send(b"\t\t\t\r600\rv")
            s.until(b"Review game-preferences.json only")
            value["poll_ms"] = 777
            preferences.write_text(json.dumps(value), encoding="utf8")
            s.send(b"\r")
            s.until(b"changed externally")
            s.process.send_signal(signal.SIGINT)
            s.finished()
            assert json.loads(preferences.read_text())["poll_ms"] == 777
            checks += 1
        value["target_executable"] = "x" * 124 + ".exe"
        preferences.write_text(json.dumps(value), encoding="utf8")
        with Session() as s:
            fcntl.ioctl(s.master, termios.TIOCSWINSZ, struct.pack("HHHH", 12, 40, 0, 0))
            s.send(b"v" + b"\x1b[6~" * 40)
            s.until(b"[End of game defaults]")
            s.process.send_signal(signal.SIGINT)
            s.finished()
            checks += 1
        with Session(running=True) as s:
            s.until(b"Effective: off (frozen)")
            s.send(b" v\r")
            s.until(b"Saved game defaults")
            s.until(b"Effective: off (frozen)")
            assert json.loads(preferences.read_text())["mode"] == "coexist"
            checks += 2
            s.send(b"\x1b")
            s.until(b"CanaryMiner / Settings")
            s.send(b"\x1b")
            s.until(b"[S]")
            s.send(b"q")
            s.finished()
            checks += 1
        assert not (state / "ui-preferences.json").exists()
        checks += 1
    print(f"game settings PTY: {checks} save/discard/conflict/resize/frozen-session checks passed")


if __name__ == "__main__":
    main()
