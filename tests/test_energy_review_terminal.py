"""Native energy review, original-input replay and real controlling-terminal SIGINT; all energy synthetic."""
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
    if os.name != 'posix':
        raise SystemExit('POSIX PTY test; Windows uses native state/renderer/process checks')
    import fcntl
    import pty
    import termios
    fixture, helper, cli = [str(Path(p).resolve()) for p in sys.argv[1:4]]
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-energy-review-') as directory:
        root = Path(directory)
        subprocess.run([fixture, '--prepare', str(root)], check=True, capture_output=True, timeout=5)
        journal = root / 'fixture-energy-review.db.energy-recovery.json'
        before = journal.read_bytes()

        def facts():
            return subprocess.check_output([fixture, '--facts', str(root)], timeout=5).decode().strip()

        assert facts() == 'Prepared 0 0 0 0'
        rejected = subprocess.run([fixture, '--settings', helper, str(root)], capture_output=True, timeout=5)
        assert rejected.returncode == 2 and journal.read_bytes() == before
        checks += 2

        def session(work, command=None):
            nonlocal checks
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 40, 100, 0, 0))

            def own_terminal():
                os.setsid()
                fcntl.ioctl(0, termios.TIOCSCTTY, 0)

            child = subprocess.Popen(command or [fixture, '--settings', helper, str(root)], stdin=slave, stdout=slave, stderr=slave, preexec_fn=own_terminal)
            os.close(slave)
            transcript = b''

            def until(needle, fresh=False):
                nonlocal transcript
                if fresh:
                    transcript = b''
                deadline = time.monotonic() + 8
                while needle not in transcript:
                    if time.monotonic() > deadline:
                        raise AssertionError('energy review terminal readiness deadline: ' + repr(needle))
                    if select.select([master], [], [], 0.05)[0]:
                        try:
                            data = os.read(master, 65536)
                        except OSError:
                            raise AssertionError('energy review terminal closed before expected state') from None
                        if not data:
                            raise AssertionError('energy review terminal EOF')
                        transcript += data
                return transcript

            def send(data):
                os.write(master, data)

            try:
                until(b'CanaryMiner / Settings')
                send(b'\t ' + b'\x1b[D' * 3 + b'\x1b[B' * 3 + b'\r')
                until(b'Energy history / durable capture status', True)
                work(send, until)
                deadline = time.monotonic() + 5
                while child.poll() is None and time.monotonic() < deadline:
                    if select.select([master], [], [], 0.05)[0]:
                        try:
                            transcript += os.read(master, 65536)
                        except OSError:
                            break
                assert child.wait(timeout=1) == 0
                assert not (root / 'ui-preferences.json').exists()
                checks += 2
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait(timeout=5)
                os.close(master)

        def ordinary(send, until):
            nonlocal checks
            until(b'no authorized durable capture owner is active')
            send(b'ri \rf')
            until(b'No active durable collector', True)
            assert journal.read_bytes() == before and facts() == 'Prepared 0 0 0 0'
            checks += 2
            send(b'q')

        config = root / 'synthetic-config.json'
        config.write_text(json.dumps({'schema_version': 1, 'state_dir': str(root)}), encoding='utf8')
        session(ordinary, [cli, 'settings', '--config', str(config)])

        def inspect_cancel(send, until):
            nonlocal checks
            until(b'NeedsReview')
            screen = until(b'PRIVATE identity')
            assert b'fixture.private-original' not in screen and b'123456' not in screen
            assert journal.read_bytes() == before
            send(b'r\r')
            until(b'Reveal with I', True)
            assert journal.read_bytes() == before
            send(b'i')
            until(b'fixture.private-original', True)
            until(b'MISSING')
            send(b' ')
            until(b'[x] Replay EXACT original', True)
            send(b'\x1b')
            until(b'Review cancelled', True)
            assert journal.read_bytes() == before and facts() == 'Prepared 0 0 0 0'
            checks += 5
            send(b'q')

        session(inspect_cancel)

        def retry(send, until):
            nonlocal checks
            until(b'NeedsReview')
            send(b'ri ')
            until(b'[x] Replay EXACT original', True)
            send(b'\r\r\r')
            until(b'Recovery: Confirmed', True)
            until(b'Exact original confirmed with persisted receipt')
            assert facts() == 'Confirmed 1 123456 1000 1800000001000'
            assert journal.read_bytes() != before
            send(b'r')
            until(b'No reviewable pending original', True)
            assert facts() == 'Confirmed 1 123456 1000 1800000001000'
            checks += 3
            send(b'\x03')

        session(retry)
        assert not list(root.glob('*preferences.json')) and not list(root.glob('*worker-starts*'))
        checks += 1
    print(f'energy review PTY: {checks} checks; ordinary unavailable/private default/cancel/one exact retry/SIGINT passed')


if __name__ == '__main__':
    main()
