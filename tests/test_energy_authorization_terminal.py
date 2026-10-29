"""Native draft/review/save/cancel and controlling-terminal stop; synthetic UUIDs and no collection."""
from contextlib import closing
import http.client
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
    runtime = str(Path(sys.argv[5]).resolve()) if len(sys.argv) >= 6 and sys.argv[4] == '--runtime' else None
    supervisor = str(Path(sys.argv[6]).resolve()) if runtime and len(sys.argv) == 7 else None
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-authorization-pty-') as directory:
        root = Path(directory)
        file = root / 'energy-persistence.json'
        rejected = subprocess.run([fixture, helper, str(root)], capture_output=True, timeout=5)
        assert rejected.returncode == 2 and not file.exists()
        checks += 1

        def session(work, command=None, supervised=False):
            nonlocal checks
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 44, 120, 0, 0))

            def own_terminal():
                os.setsid()
                fcntl.ioctl(0, termios.TIOCSCTTY, 0)

            default = [runtime, 'settings', '--config', str(config)] if runtime else [fixture, helper, str(root)]
            child = subprocess.Popen(command or default, stdin=slave, stdout=slave, stderr=slave, preexec_fn=own_terminal)
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
                if supervised:
                    until(b'[S]')
                    send(b's')
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
        config.write_text(json.dumps({'schema_version': 1, 'state_dir': str(root),
                                     'devices': ['GPU-aaaaaaaa-0000-0000-0000-000000000001']}), encoding='utf8')
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
        if runtime:
            def suppressed(send, until):
                nonlocal checks
                until(b'--no-energy overrides provisioning')
                until(b'Current capture: not active')
                assert file.read_bytes() == before and not (root / 'energy-persistence.json.collector.lock').exists()
                checks += 2
                send(b'q')

            session(suppressed, [runtime, 'settings', '--config', str(config), '--no-energy'])
            cfg = json.loads(config.read_text())
            cfg['devices'] = ['0']
            config.write_text(json.dumps(cfg), encoding='utf8')

            def no_discovery(send, until):
                nonlocal checks
                until(b'Enrollment unavailable: select full explicit GPU UUIDs')
                assert file.read_bytes() == before
                checks += 1
                send(b'q')

            session(no_discovery)
        assert file.read_bytes() == before and not list(root.glob('*worker-starts*'))
        checks += 1
        if supervisor:
            first_session = None

            def active_off(send, until):
                nonlocal checks, first_session
                screen = until(b'Current capture: frozen active plan')
                saved = json.loads(file.read_bytes())
                first_session = saved[8]
                assert first_session and first_session.encode() not in screen and saved[6].encode() not in screen
                with closing(http.client.HTTPConnection('127.0.0.1', int((root / 'fixture-api-port').read_text()), timeout=3)) as connection:
                    connection.request('GET', '/api/v1/stats')
                    response = connection.getresponse()
                    public = response.read()
                    assert response.status == 200
                assert all(token.encode() not in public for token in (first_session, saved[6], saved[7][3], str(root), 'energy-persistence'))
                send(b' v\r')
                until(b'CONFIRMED saved', True)
                until(b'Current capture: frozen active plan')
                assert json.loads(file.read_bytes())[3][0] is False
                checks += 5
                send(b'q')

            command = [supervisor, '--energy-runtime-session-test', str(root)]
            session(active_off, command, True)

            def inactive_on(send, until):
                nonlocal checks
                until(b'Current capture: not active')
                until(b'Persist board-energy history: OFF')
                send(b' v\r')
                until(b'CONFIRMED saved', True)
                until(b'Current capture: not active')
                assert json.loads(file.read_bytes())[8] == first_session
                checks += 2
                send(b'q')

            session(inactive_on, command, True)

            def fresh(send, until):
                nonlocal checks
                until(b'Current capture: frozen active plan')
                assert json.loads(file.read_bytes())[8] != first_session
                checks += 1
                send(b'\x03')

            session(fresh, command, True)
    print(f'energy {"runtime" if runtime else "authorization"} PTY: {checks} checks passed; synthetic host only')


if __name__ == '__main__':
    main()
