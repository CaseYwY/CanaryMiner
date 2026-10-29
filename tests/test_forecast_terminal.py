"""Controlling-terminal verification of the synthetic supervisor and real background query owner."""
from contextlib import closing
import http.client
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
        raise SystemExit('POSIX PTY; native state/process tests cover Windows')
    import fcntl
    import pty
    import termios
    program, writer, reader, stalled = [str(Path(p).resolve()) for p in sys.argv[1:5]]
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-forecast-pty-') as directory:
        root = Path(directory)
        for mode in ('complete', 'no-market', 'no-energy', 'hang'):
            state = root / mode
            rejected = subprocess.run([program, writer, reader, str(state), mode], capture_output=True, timeout=5)
            assert rejected.returncode == 2 and not state.exists()
            checks += 1
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 64, 140, 0, 0))

            def own_terminal():
                os.setsid()
                fcntl.ioctl(0, termios.TIOCSCTTY, 0)

            child = subprocess.Popen([program, writer, stalled if mode == 'hang' else reader, str(state), mode],
                                     stdin=slave, stdout=slave, stderr=slave, preexec_fn=own_terminal)
            os.close(slave)
            transcript = b''

            def until(needle):
                nonlocal transcript
                deadline = time.monotonic() + 10
                while needle not in transcript:
                    if time.monotonic() > deadline:
                        raise AssertionError('forecast terminal readiness: ' + repr(needle))
                    if select.select([master], [], [], 0.05)[0]:
                        transcript += os.read(master, 65536)
                return transcript

            try:
                until(b'[Q]')
                if mode == 'hang':
                    deadline = time.monotonic() + 5
                    while not (state / 'fixture-query-hang.db.fixture-ready').exists():
                        assert time.monotonic() < deadline
                        time.sleep(0.01)
                    until(b'Income: N/A')
                else:
                    until(b'Today forecast: 33.00000000 PRL')
                    until(b'Actual credited 24h: 23.00000000 PRL')
                    until(b'Estimated net today: 80.25 USD' if mode == 'complete' else b'Estimated net today: N/A USD')
                    if mode in ('no-market', 'no-energy'):
                        until(('Session override: --' + mode).encode())
                        checks += 1
                    assert b'synthetic-worker-A' not in transcript and b'fixture.local-measured-profile' not in transcript
                    checks += 3
                port = int((state / 'fixture-api-port').read_text())
                with closing(http.client.HTTPConnection('127.0.0.1', port, timeout=3)) as connection:
                    connection.request('GET', '/api/v1/stats')
                    response = connection.getresponse()
                    public = response.read()
                    assert response.status == 200
                assert all(value not in public for value in (b'synthetic-worker-A', b'synthetic-snapshot-', b'fixture.reward-model', b'forecast', str(state).encode()))
                os.write(master, b'\x03' if mode == 'hang' else b'q')
                deadline = time.monotonic() + 5
                while child.poll() is None and time.monotonic() < deadline:
                    if select.select([master], [], [], 0.05)[0]:
                        try:
                            transcript += os.read(master, 65536)
                        except OSError:
                            break
                assert child.wait(timeout=1) == 0
                checks += 3
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait(timeout=5)
                os.close(master)
    print(f'forecast owner PTY: {checks} checks passed; synthetic workers and loopback only')


if __name__ == '__main__':
    main()
