"""Native draft/review/save/cancel and controlling-terminal stop; synthetic UUIDs and no collection."""
from contextlib import closing
import json
import os
from pathlib import Path
import select
import sqlite3
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
    with tempfile.TemporaryDirectory(prefix='canary-authorization-pty-') as directory:
        root = Path(directory)
        file = root / 'energy-persistence.json'
        rejected = subprocess.run([fixture, helper, str(root)], capture_output=True, timeout=5)
        assert rejected.returncode == 2 and not file.exists()
        checks += 1

        def session(work, command=None):
            nonlocal checks
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 44, 120, 0, 0))

            def own_terminal():
                os.setsid()
                fcntl.ioctl(0, termios.TIOCSCTTY, 0)

            child = subprocess.Popen(command or [fixture, helper, str(root)], stdin=slave, stdout=slave, stderr=slave, preexec_fn=own_terminal)
            os.close(slave)
            transcript = b''

            def until(needle, fresh=False):
                nonlocal transcript
                if fresh:
                    transcript = b''
                deadline = time.monotonic() + 8
                while needle not in transcript:
                    if time.monotonic() > deadline:
                        raise AssertionError('authorization terminal readiness deadline: ' + repr(needle))
                    if select.select([master], [], [], 0.05)[0]:
                        try:
                            data = os.read(master, 65536)
                        except OSError:
                            raise AssertionError('authorization terminal closed before expected state') from None
                        if not data:
                            raise AssertionError('authorization terminal EOF')
                        transcript += data
                return transcript

            def send(data):
                os.write(master, data)

            try:
                until(b'CanaryMiner / Settings')
                send(b'\x1b[C' * 2 + b'\t' + b'\x1b[B' * 3 + b'\r')
                until(b'Energy persistence / independent authorization', True)
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
            until(b'no authorization helper owner supplied')
            send(b'evi \r')
            until(b'No editable saved snapshot', True)
            assert not file.exists()
            checks += 1
            send(b'q')

        config = root / 'synthetic-config.json'
        config.write_text(json.dumps({'schema_version': 1, 'state_dir': str(root)}), encoding='utf8')
        session(ordinary, [cli, 'settings', '--config', str(config)])

        def cancel(send, until):
            nonlocal checks
            screen = until(b'Missing / disabled defaults')
            assert b'GPU-aaaaaaaa' not in screen and not file.exists()
            send(b'\x1b[B\r2000\r')
            until(b'Draft only', True)
            send(b'v')
            until(b'REVIEW SAVE', True)
            send(b'\x1b')
            until(b'Review cancelled', True)
            assert not file.exists()
            send(b'de\r')
            until(b'I reveals the selected/private original', True)
            assert not file.exists()
            send(b'i ')
            until(b'[x] Reviewed exact selection', True)
            until(b'GPU-aaaaaaaa')
            send(b'\x1b')
            until(b'Review cancelled', True)
            assert not file.exists()
            checks += 4
            send(b'q')

        session(cancel)

        def enroll(send, until):
            nonlocal checks
            until(b'Missing / disabled defaults')
            send(b'ei ')
            until(b'[x] Reviewed exact selection', True)
            send(b'\r')
            until(b'CONFIRMED saved', True)
            registered = json.loads(file.read_bytes())
            assert registered[5] == 2 and registered[3][0] is False and registered[8] == ''
            send(b'i ')
            until(b'Persist board-energy history: ON', True)
            send(b'v')
            until(b'REVIEW SAVE', True)
            send(b'\r')
            until(b'CONFIRMED saved', True)
            saved = json.loads(file.read_bytes())
            assert saved[3][0] is True and saved[8] == '' and saved[6:8] == registered[6:8]
            database = root / ('energy-history.' + saved[6] + '.db')
            with closing(sqlite3.connect(database)) as db:
                assert db.execute('SELECT sequence FROM meta').fetchone()[0] == 0
                assert db.execute('SELECT COUNT(*) FROM events').fetchone()[0] == 0
            assert b'GPU-' not in file.read_bytes()
            checks += 4
            send(b'\x03')

        session(enroll)
        before = file.read_bytes()

        def reopen(send, until):
            nonlocal checks
            screen = until(b'Persist board-energy history: ON')
            saved = json.loads(before)
            assert saved[6].encode() not in screen and saved[7][3].encode() not in screen
            send(b' ')
            until(b'Persist board-energy history: OFF', True)
            send(b'\x1b')
            until(b'Unsaved draft', True)
            send(b'd')
            until(b'CanaryMiner / Settings', True)
            assert file.read_bytes() == before
            checks += 2
            send(b'q')

        session(reopen)
        assert file.read_bytes() == before and not list(root.glob('*worker-starts*'))
        checks += 1
    print(f'energy authorization PTY: {checks} checks passed; no collection or mining started')


if __name__ == '__main__':
    main()
