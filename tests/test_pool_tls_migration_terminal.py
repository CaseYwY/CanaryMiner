"""Actual supervisor/settings migration review with owned CPU workers and loopback stats."""
import copy
import json
import os
from pathlib import Path
import re
import select
import signal
import struct
import subprocess
import sys
import tempfile
import time


def main():
    if os.name != "posix":
        raise SystemExit("POSIX PTY only; declaration and policy checks are cross-platform")
    import fcntl
    import pty
    import termios

    executable = str(Path(sys.argv[1]).resolve(strict=True))
    checks = 0
    default_rule = {"mode": "unchanged", "tls_url": "", "plaintext_url": "", "plaintext_consent": False}
    original = {
        "schema_version": 1,
        "user_pools": [copy.deepcopy(default_rule), {
            "mode": "unchanged", "tls_url": "stratum+ssl://second-fixture.test:8548",
            "plaintext_url": "stratum+tcp://second-fixture.test:7548", "plaintext_consent": False,
        }],
        "donation": copy.deepcopy(default_rule),
    }
    migrated = copy.deepcopy(original)
    migrated["user_pools"][0] = {
        "mode": "strict_tls", "tls_url": "stratum+ssl://prl-us.kryptex.network:8048",
        "plaintext_url": "", "plaintext_consent": False,
    }

    class Session:
        def __init__(self, state):
            self.state = state
            state.mkdir()
            self.preferences = state / "pool-security-preferences.json"
            self.original = (json.dumps(original, separators=(",", ":")) + "\n").encode()
            self.preferences.write_bytes(self.original)
            self.config = state / "config.json"
            self.config_bytes = (json.dumps({
                "schema_version": 1, "wallet": "MIGRATION-CPU-FIXTURE", "worker": "cpu-only",
                "pools": ["kryptex"], "donation": False, "state_dir": str(state),
            }) + "\n").encode()
            self.config.write_bytes(self.config_bytes)
            (state / "runtime-preferences.json").write_text(json.dumps({
                "schema_version": 1, "work_wait_ms": 0, "history_enabled": False,
            }), encoding="utf-8")
            (state / "fixture-pool-transport.txt").write_text("plaintext\n", encoding="ascii")
            self.master, slave = pty.openpty()
            try:
                self.resize(120, 36, slave)
                self.process = subprocess.Popen(
                    [executable, "--pool-tls-migration-session-test", str(state)],
                    stdin=slave, stdout=slave, stderr=slave, start_new_session=True,
                )
            except BaseException:
                os.close(self.master)
                raise
            finally:
                os.close(slave)
            self.wire = b""
            self.frames = []
            self.ansi = re.compile(rb"\x1b\[[0-?]*[ -/]*[@-~]")

        def resize(self, columns, rows, fd=None):
            fcntl.ioctl(self.master if fd is None else fd, termios.TIOCSWINSZ,
                        struct.pack("HHHH", rows, columns, 0, 0))

        def pump(self, timeout=0.1):
            ready, _, _ = select.select([self.master], [], [], timeout)
            if not ready:
                return
            try:
                data = os.read(self.master, 65536)
            except OSError:
                raise AssertionError("migration terminal closed before expected frame") from None
            assert data, "migration terminal EOF"
            self.wire += data
            while b"\x1b[J" in self.wire:
                end = self.wire.index(b"\x1b[J") + len(b"\x1b[J")
                chunk, self.wire = self.wire[:end], self.wire[end:]
                start = chunk.rfind(b"\x1b[H")
                if start >= 0:
                    self.frames.append(self.ansi.sub(b"", chunk[start:]).decode("utf-8", errors="strict"))
            assert len(self.wire) < 1024 * 1024, "unbounded migration terminal frame"

        def until(self, *needles, after=None, timeout=8, predicate=None):
            floor = len(self.frames) if after is None else after
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                for frame in self.frames[floor:]:
                    if all(needle in " ".join(frame.split()) for needle in needles) and (predicate is None or predicate(frame)):
                        return frame
                floor = len(self.frames)
                self.pump()
            tail = self.frames[-1] if self.frames else repr(self.wire[-1500:])
            raise AssertionError(f"No new migration frame containing {needles!r}; last frame:\n{tail}")

        def send(self, keys):
            floor = len(self.frames)
            os.write(self.master, keys)
            return floor

        def __enter__(self):
            try:
                self.until("[S]", "Plaintext", "7048")
                self.starts = (self.state / "fixture-worker-starts.txt").read_bytes()
                assert self.starts == b"start\n", "fixture must begin with exactly one owned worker"
                floor = self.send(b"s")
                self.until("CanaryMiner / Settings", after=floor)
                floor = self.send(b"\x1b[C" * 6 + b"\t\r")
                self.until("Pools / TLS declarations", "[M] TLS review", after=floor)
                return self
            except BaseException:
                self.__exit__(*sys.exc_info())
                raise

        def unchanged_session(self):
            assert self.config.read_bytes() == self.config_bytes
            assert (self.state / "fixture-worker-starts.txt").read_bytes() == self.starts
            assert not (self.state / "accepted-history.sqlite3").exists()

        def current_plaintext(self, frame):
            text = " ".join(frame.split())
            assert "First user pool transport: Plaintext (port 7048)" in text
            assert "Effective first-user policy: Plaintext" in text
            assert "First-user host (frozen): prl-us.kryptex.network" in text
            assert "Verified TLS" not in text

        def finish(self):
            self.send(b"q")
            self.process.wait(timeout=5)
            assert self.process.returncode == 0
            self.unchanged_session()

        def __exit__(self, *_):
            try:
                if self.process.poll() is None:
                    self.process.send_signal(signal.SIGTERM)
                    try:
                        self.process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        try:
                            os.killpg(self.process.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                        self.process.wait(timeout=3)
            finally:
                os.close(self.master)

    with tempfile.TemporaryDirectory(prefix="canary-pool-migration-pty-") as directory:
        root = Path(directory)
        with Session(root / "save") as session:
            floor = session.send(b"m")
            frame = session.until("Strict TLS migration review", "Review pool-security-preferences.json only", after=floor)
            session.current_plaintext(frame)
            assert "Configured source: stratum+tcp://prl-us.kryptex.network:7048" in " ".join(frame.split())
            assert "Proposed TLS: stratum+ssl://prl-us.kryptex.network:8048" in " ".join(frame.split())
            assert session.preferences.read_bytes() == session.original
            session.unchanged_session()
            checks += 5

            # Review must expose the full before/after at 20x12 without requiring a Save.
            def narrow_review(frame):
                lines = frame.splitlines()
                # This English fixture uses printable ASCII only, so each character is one terminal cell.
                return 0 < len(lines) <= 12 and all(
                    len(line) <= 19 and all(32 <= ord(char) < 127 for char in line) for line in lines
                )

            session.resize(20, 12)
            floor = session.send(b"\x1b[5~")
            review_frames = [session.until("[Enter] Save", after=floor, predicate=lambda frame:
                narrow_review(frame) and "[EndofTLSreview]" not in "".join(frame.split()))]
            for _ in range(70):
                floor = session.send(b"\x1b[6~")
                frame = session.until("[Enter] Save", after=floor, predicate=narrow_review)
                review_frames.append(frame)
                if "[EndofTLSreview]" in "".join(frame.split()):
                    break
            else:
                raise AssertionError("short migration Review cannot reach its end")
            compact_frames = ["".join(frame.split()) for frame in review_frames]
            compact = "".join(compact_frames)
            assert "Before:" in compact and "After:" in compact
            # Rejoin wrapped pieces within one complete frame, never unrelated prefixes from different pages.
            assert any("stratum+tcp://prl-us.kryptex.network:7048" in frame for frame in compact_frames)
            assert any("stratum+ssl://prl-us.kryptex.network:8048" in frame for frame in compact_frames)
            assert session.preferences.read_bytes() == session.original
            checks += 5

            session.resize(120, 36)
            floor = session.send(b"\r")
            frame = session.until("Saved declarations only", after=floor)
            session.current_plaintext(frame)
            assert json.loads(session.preferences.read_bytes()) == migrated
            session.unchanged_session()
            checks += 3
            saved = session.preferences.read_bytes()
            # A pre-existing strict declaration is never replaced or automatically reviewed again.
            floor = session.send(b"m")
            frame = session.until("Pools / TLS declarations", after=floor)
            assert "Strict TLS migration review" not in frame
            assert session.preferences.read_bytes() == saved
            session.current_plaintext(frame)
            checks += 3
            session.finish()
            checks += 1

        with Session(root / "conflict") as session:
            floor = session.send(b"m")
            session.until("Strict TLS migration review", after=floor)
            external = session.original + b"\n"
            session.preferences.write_bytes(external)  # same values, genuinely different current-original bytes
            floor = session.send(b"\r")
            frame = session.until("Preferences UNKNOWN", "NOT Saved", after=floor)
            assert "prl-us.kryptex.network" not in frame  # private documents are hidden until explicit reveal
            assert session.preferences.read_bytes() == external
            session.unchanged_session()
            checks += 3

            # Restoring old bytes cannot revive consumed Save authority or retry the migration.
            session.preferences.write_bytes(session.original)
            floor = session.send(b"m\r")
            session.until("Preferences UNKNOWN", after=floor)
            assert session.preferences.read_bytes() == session.original
            floor = session.send(b"\x1b")
            session.until("[S]", after=floor)
            floor = session.send(b"s")
            session.until("Preferences UNKNOWN", after=floor)
            checks += 3

            floor = session.send(b"i")
            session.until("ORIGINAL REQUEST | inspected", after=floor)
            floor = session.send(b"r\x1b[6~")
            session.until("PRIVATE local review", after=floor)
            # Both complete documents must be rendered before the existing inspection acknowledgement.
            floor = session.send(b"\x1b[6~" * 30)
            session.until("ORIGINAL REQUEST | inspected", after=floor)
            floor = session.send(b"\t\x1b[6~")
            session.until("CURRENT PARSED VALUES", after=floor)
            floor = session.send(b"\x1b[6~" * 30)
            session.until("CURRENT PARSED VALUES", after=floor)
            floor = session.send(b" \r")
            frame = session.until("Pools / TLS declarations", after=floor)
            assert "Strict TLS migration review" not in frame
            assert session.preferences.read_bytes() == session.original
            session.current_plaintext(frame)
            checks += 3

            # The accepted baseline becomes editable; only another explicit M and Save applies the proposal.
            floor = session.send(b"m")
            session.until("Strict TLS migration review", after=floor)
            assert session.preferences.read_bytes() == session.original
            floor = session.send(b"\r")
            frame = session.until("Saved declarations only", after=floor)
            assert json.loads(session.preferences.read_bytes()) == migrated
            session.current_plaintext(frame)
            session.unchanged_session()
            checks += 3
            session.finish()
            checks += 1

    print(f"pool_tls_migration_terminal: {checks} checks; real Review/Save, short terminal, external conflict, no replay or worker restart")


if __name__ == "__main__":
    if not __debug__:
        raise RuntimeError("assertions require non-optimized Python")
    main()
