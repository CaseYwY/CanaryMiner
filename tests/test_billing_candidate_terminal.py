"""Native candidate review; all owned stores, quotes and helpers are synthetic and offline."""
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
        raise SystemExit('POSIX controlling-terminal test; Windows uses native panel/owner tests')
    import fcntl
    import pty
    import termios
    fixture, stage, money, writer, retry, reader = [str(Path(p).resolve(strict=True)) for p in sys.argv[1:7]]
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-billing-candidate-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        subprocess.run([fixture, '--prepare-candidate', str(root)], check=True, capture_output=True, timeout=10)
        ledger = root / 'fixture-billing-review.db'
        files = [ledger, Path(str(ledger) + '.billing-recovery'), root / 'fixture-source.db', root / 'fixture-source.db.energy-recovery.json']
        original = [p.read_bytes() for p in files]
        staged = Path(str(ledger) + '.billing-input')

        def facts():
            return subprocess.check_output([fixture, '--facts-candidate', str(root)], timeout=10).strip()

        def session(work):
            nonlocal checks
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 28, 80, 0, 0))

            def own_terminal():
                os.setsid()
                fcntl.ioctl(0, termios.TIOCSCTTY, 0)

            child = subprocess.Popen([fixture, '--settings-candidate', stage, money, writer, retry, reader, str(root)], stdin=slave, stdout=slave, stderr=slave, preexec_fn=own_terminal)
            os.close(slave)
            transcript = b''

            def until(needle, fresh=False):
                nonlocal transcript
                if fresh:
                    transcript = b''
                end = time.monotonic() + 10
                while needle not in transcript:
                    if time.monotonic() >= end:
                        raise AssertionError('candidate terminal deadline: ' + repr(needle))
                    if select.select([master], [], [], 0.05)[0]:
                        try:
                            data = os.read(master, 65536)
                        except OSError:
                            raise AssertionError('candidate terminal closed early') from None
                        if not data:
                            raise AssertionError('candidate terminal EOF')
                        transcript += data
                return transcript

            def send(data):
                os.write(master, data)

            def resize(rows, columns):
                fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack('HHHH', rows, columns, 0, 0))

            try:
                until(b'CanaryMiner / Settings')
                send(b'\x1b[C' * 2 + b'\t' + b'\x1b[B' * 4 + b'\rp')
                screen = until(b'Candidate: REVIEWABLE', True)
                assert b'fixture-source.db' not in screen and b'370368' not in screen
                checks += 1
                work(send, until, resize)
                end = time.monotonic() + 5
                while child.poll() is None and time.monotonic() < end:
                    if select.select([master], [], [], 0.05)[0]:
                        try:
                            os.read(master, 65536)
                        except OSError:
                            break
                assert child.wait(timeout=2) == 0
                assert not list(root.glob('*preferences.json'))
                checks += 2
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait(timeout=5)
                os.close(master)

        def cancel(send, until, resize):
            nonlocal checks
            send(b'v\r ')
            until(b'before acknowledgement.', True)
            assert not staged.exists()
            send(b'i ')
            until(b'Billing / Review Submit [x]', True)
            send(b'b')
            until(b'END OF COMPLETE PRIVATE CANDIDATE DOCUMENT', True)
            resize(12, 32)
            until(b'DOCUMENT', True)
            resize(28, 80)
            send(b'\x1b')
            until(b'Review cancelled; nothing sent.', True)
            assert [p.read_bytes() for p in files] == original and facts() == b'1 0'
            checks += 3
            send(b'q')

        session(cancel)

        def stop_acknowledged(send, until, resize):
            nonlocal checks
            send(b'vi ')
            until(b'Billing / Review Submit [x]', True)
            send(b'\x03')
            checks += 1

        session(stop_acknowledged)
        assert not staged.exists() and [p.read_bytes() for p in files] == original
        checks += 1

        def submit(send, until, resize):
            nonlocal checks
            send(b'f')
            until(b'Inspection complete; current read available.', True)
            send(b'v')
            until(b'Candidate unavailable or historical; nothing sent.', True)
            send(b'g')
            until(b'Candidate: REVIEWABLE', True)
            send(b'vi ')
            until(b'Billing / Review Submit [x]', True)
            send(b'\r\r\r')
            until(b'Historical action proof received.', True)
            assert facts() == b'3 1' and staged.exists()
            assert [p.read_bytes() for p in files[2:]] == original[2:]
            after = staged.read_bytes()
            send(b'v \r')
            until(b'Candidate unavailable or historical; nothing sent.', True)
            assert staged.read_bytes() == after and facts() == b'3 1'
            checks += 3
            send(b'q')

        session(submit)
    print(f'billing candidate PTY: {checks} checks; private/full-tail/resize/cancel/SIGINT/recompute/one Submit passed')


if __name__ == '__main__':
    main()
