"""Native billing review with only synthetic owned stores/helpers; no GPU, pool, network or account values."""
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
        raise SystemExit('POSIX controlling-terminal test; Windows uses renderer/controller/process tests')
    import fcntl
    import pty
    import termios
    fixture, stage, money, writer, retry, cli = [str(Path(p).resolve(strict=True)) for p in sys.argv[1:7]]
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-billing-review-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        command = [fixture, '--settings', stage, money, writer, retry, str(root)]
        subprocess.run([fixture, '--prepare', str(root)], check=True, capture_output=True, timeout=10)
        ledger = root / 'fixture-billing-review.db'
        staged = Path(str(ledger) + '.billing-input')
        original = staged.read_bytes()

        def facts():
            return subprocess.check_output([fixture, '--facts', str(root)], timeout=10).strip()

        assert facts() == b'0 0' and not ledger.exists()
        noninteractive = subprocess.run(command, capture_output=True, timeout=10)
        assert noninteractive.returncode == 2 and staged.read_bytes() == original and not ledger.exists()
        checks += 2

        def session(work, args=None):
            nonlocal checks
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 28, 80, 0, 0))

            def own_terminal():
                os.setsid()
                fcntl.ioctl(0, termios.TIOCSCTTY, 0)

            child = subprocess.Popen(args or command, stdin=slave, stdout=slave, stderr=slave, preexec_fn=own_terminal)
            os.close(slave)
            transcript = b''

            def until(needle, fresh=False):
                nonlocal transcript
                if fresh:
                    transcript = b''
                deadline = time.monotonic() + 10
                while needle not in transcript:
                    if time.monotonic() >= deadline:
                        raise AssertionError('billing terminal readiness deadline: ' + repr(needle))
                    if select.select([master], [], [], 0.05)[0]:
                        try:
                            data = os.read(master, 65536)
                        except OSError:
                            raise AssertionError('billing terminal closed before expected state') from None
                        if not data:
                            raise AssertionError('billing terminal EOF before expected state')
                        transcript += data
                return transcript

            def send(data):
                os.write(master, data)

            def resize(rows, columns):
                fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack('HHHH', rows, columns, 0, 0))

            try:
                until(b'CanaryMiner / Settings')
                # Independent unsaved color draft, then Electricity field five.
                send(b'\t\x1b[B ' + b'\x1b[C' * 2 + b'\x1b[B' * 4 + b'\r')
                until(b'Billing / recovery status', True)
                work(send, until, resize)
                deadline = time.monotonic() + 5
                while child.poll() is None and time.monotonic() < deadline:
                    if select.select([master], [], [], 0.05)[0]:
                        try:
                            transcript += os.read(master, 65536)
                        except OSError:
                            break
                assert child.wait(timeout=2) == 0
                assert not (root / 'ui-preferences.json').exists()
                checks += 2
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait(timeout=5)
                os.close(master)

        def unavailable(send, until, resize):
            nonlocal checks
            until(b'no approved billing owner')
            send(b'ncrei \rf')
            until(b'Inspection unavailable; nothing queued.', True)
            assert staged.read_bytes() == original and not ledger.exists()
            checks += 1
            send(b'q')

        config = root / 'fixture-config.json'
        config.write_text(json.dumps({'schema_version': 1, 'state_dir': str(root)}), encoding='utf8')
        session(unavailable, [cli, 'settings', '--config', str(config)])

        def cancel(send, until, resize):
            nonlocal checks
            screen = until(b'PRIVATE values hidden')
            assert b'fixture-billing-review.db' not in screen
            until(b'Stage: Receiving')
            send(b'c\r')
            until(b'Nothing sent.', True)
            assert staged.read_bytes() == original and not ledger.exists()
            send(b'i')
            until(b'Private values revealed locally.', True)
            send(b'b')
            until(b'END OF COMPLETE PRIVATE REVIEW DOCUMENT', True)
            resize(12, 32)
            until(b'DOCUMENT', True)
            send(b'\x1b[A')
            resize(28, 80)
            send(b't ')
            until(b'Billing / Review Commit [x]', True)
            send(b'\x1b')
            until(b'Review cancelled; nothing sent.', True)
            assert staged.read_bytes() == original and facts() == b'0 0'
            checks += 4
            send(b'q')

        session(cancel)

        def commit(send, until, resize):
            nonlocal checks
            until(b'Stage: Receiving')
            send(b'ci ')
            until(b'Billing / Review Commit [x]', True)
            send(b'\r\r\r')
            until(b'Historical action proof received.', True)
            assert facts() == b'1 0' and staged.read_bytes() != original
            before = staged.read_bytes()
            send(b'f')
            until(b'Inspection complete; current read available.', True)
            until(b'Stage: Complete')
            send(b'c\r')
            until(b'Action unavailable for this read; nothing sent.', True)
            assert staged.read_bytes() == before and facts() == b'1 0'
            checks += 3
            send(b'\x03')

        session(commit)
        assert not list(root.glob('*preferences.json')) and not list(root.glob('*worker-starts*'))
        checks += 1
    print(f'billing review PTY: {checks} checks; default-off/private/complete-tail/narrow/cancel/one Commit/inspect/SIGINT passed')


if __name__ == '__main__':
    main()
