"""Actual POSIX supervisor/HTTP/settings path with CPU worker observations, no pool or GPU."""
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
        raise SystemExit("POSIX PTY only; native state and JSON checks are cross-platform")
    import fcntl
    import pty
    import termios

    executable = str(Path(sys.argv[1]).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix="canary-pool-status-pty-") as directory:
        state = Path(directory)
        (state / "runtime-preferences.json").write_text(json.dumps({
            "schema_version": 1, "work_wait_ms": 0, "history_enabled": False,
        }), encoding="utf-8")
        control = state / "fixture-pool-transport.txt"

        def scenario(name):
            pending = control.with_suffix(".next")
            pending.write_text(name + "\n", encoding="ascii")
            pending.replace(control)

        scenario("verified")
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 36, 120, 0, 0))
        process = subprocess.Popen(
            [executable, "--pool-transport-session-test", str(state)],
            stdin=slave, stdout=slave, stderr=slave, start_new_session=True,
        )
        os.close(slave)
        wire = b""
        frames = []
        ansi = re.compile(rb"\x1b\[[0-?]*[ -/]*[@-~]")

        def pump(timeout=0.1):
            nonlocal wire
            ready, _, _ = select.select([master], [], [], timeout)
            if not ready:
                return
            try:
                data = os.read(master, 65536)
            except OSError:
                raise AssertionError("pool transport terminal closed before expected frame") from None
            assert data, "pool transport terminal EOF"
            wire += data
            # Assertions use complete new frames, never text left in the transcript.
            while b"\x1b[J" in wire:
                end = wire.index(b"\x1b[J") + len(b"\x1b[J")
                chunk, wire = wire[:end], wire[end:]
                start = chunk.rfind(b"\x1b[H")
                if start >= 0:
                    frames.append(ansi.sub(b"", chunk[start:]).decode("utf-8", errors="strict"))
            assert len(wire) < 1024 * 1024, "unbounded terminal frame"

        def until(*needles, after=None, timeout=10):
            floor = len(frames) if after is None else after
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                for frame in frames[floor:]:
                    if all(needle in " ".join(frame.split()) for needle in needles):
                        return frame
                floor = len(frames)
                pump()
            tail = frames[-1] if frames else repr(wire[-1500:])
            raise AssertionError(f"No new frame containing {needles!r}; last frame:\n{tail}")

        def send(keys):
            floor = len(frames)
            os.write(master, keys)
            return floor

        try:
            until("[S]", "Verified TLS")
            checks += 1
            floor = send(b"d")
            until("Verified TLS", "DETAILS", after=floor)
            checks += 1
            floor = send(b"s")
            until("CanaryMiner / Settings", after=floor)
            floor = send(b"\x1b[C" * 6 + b"\t\r")
            until("Pools / TLS declarations", "Verified TLS", after=floor)
            starts = (state / "fixture-worker-starts.txt").read_bytes()
            checks += 1

            # The open panel must follow actual observations without being reopened.
            scenario("plaintext")
            frame = until("Plaintext", "7048")
            assert "Verified TLS" not in frame
            checks += 2
            scenario("disconnected")
            frame = until("Disconnected", "verification")
            assert "Verified TLS" not in frame and "Plaintext | port 7048" not in frame
            checks += 2
            scenario("connecting")
            frame = until("Connecting")
            assert "verification" not in frame and "Verified TLS" not in frame
            checks += 2
            scenario("verified")
            until("Verified TLS", "8048")
            scenario("stale")
            # Worker HTTP stays healthy, but the original observation stops advancing.
            frame = until("Unknown", timeout=12)
            assert "Verified TLS" not in frame
            checks += 2
            scenario("verified")
            until("Verified TLS", "8048")
            checks += 1
            scenario("malformed")
            frame = until("Unknown", timeout=3)
            assert "Verified TLS" not in frame
            scenario("verified")
            until("Verified TLS", "8048")
            checks += 2

            # Draft edits and saves must leave the running policy and worker intact.
            floor = send(b"\t  \t\rstratum+ssl://saved-fixture.test:8048\r\t\rstratum+tcp://saved-fixture.test:7048\rv")
            until("Review pool-security-preferences.json only", after=floor)
            floor = send(b"\r")
            frame = until("Pools / TLS declarations", "Verified TLS", after=floor)
            preferences = json.loads((state / "pool-security-preferences.json").read_text(encoding="utf-8"))
            assert preferences["user_pools"][0]["mode"] == "prefer_tls"
            assert preferences["user_pools"][0]["plaintext_consent"] is False
            assert "Strict TLS" in frame and "frozen" in frame
            assert "First-user host (frozen): pool-fixture.test" in frame
            assert (state / "fixture-worker-starts.txt").read_bytes() == starts
            checks += 5

            saved = (state / "pool-security-preferences.json").read_bytes()
            # An invalid draft cannot enter Review, but must still allow viewing current status.
            floor = send(b"\x1b[A\r\x7f\rv")
            until("An explicit TLS endpoint is required", after=floor)
            fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 12, 20, 0, 0))
            floor = send(b"\x1b[5~")
            until("Verified TLS", "8048", after=floor)
            assert (state / "pool-security-preferences.json").read_bytes() == saved
            checks += 2
            scenario("disconnected")
            frame = until("Disconnected")
            assert "Verified TLS" not in " ".join(frame.split())
            checks += 1
            fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 36, 120, 0, 0))
            floor = send(b"\t")  # Resume field navigation without replacing the invalid draft.
            frame = until("Pools / TLS declarations *", after=floor)
            assert "saved-fixture.test:7048" in frame
            assert (state / "pool-security-preferences.json").read_bytes() == saved
            checks += 2

            began = time.monotonic()
            send(b"q")
            process.wait(timeout=5)
            assert process.returncode == 0 and time.monotonic() - began < 4
            assert (state / "fixture-worker-starts.txt").read_bytes() == starts
            checks += 2
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=3)
            os.close(master)
    print(f"pool_transport_terminal: {checks} checks; CPU observations through real worker HTTP, live panel, save and stop")


if __name__ == "__main__":
    main()
