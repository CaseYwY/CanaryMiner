"""Actual POSIX dashboard + owned fake mining worker + deliberately held private helper; no GPU/game/external source."""
import json
import os
from pathlib import Path
import select
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time


def main():
    if os.name != 'posix':
        raise SystemExit('POSIX PTY only; Windows exercises the native owner/state/store cases')
    import fcntl
    import pty
    import termios
    supervisor, held_read, held_write, lost_ack = [Path(value).resolve() for value in sys.argv[1:5]]
    checks = 0
    for name, helper in (('held-read', held_read), ('held-write', held_write), ('lost-ack', lost_ack)):
        with tempfile.TemporaryDirectory(prefix='canary-preferences-live-') as directory:
            root = Path(directory)
            binary = root / 'supervisor'
            shutil.copy2(supervisor, binary)
            shutil.copy2(helper, root / 'canary_preferences_worker')
            state = root / 'state'
            state.mkdir()
            (state / 'runtime-preferences.json').write_text(json.dumps({'schema_version': 1, 'work_wait_ms': 0, 'history_enabled': False}))
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 100, 0, 0))
            child = subprocess.Popen([str(binary), '--dashboard-session-test', str(state)], stdin=slave, stdout=slave, stderr=slave, start_new_session=True)
            os.close(slave)
            transcript = b''

            def until(needle, fresh=False):
                nonlocal transcript
                if fresh:
                    transcript = b''
                deadline = time.monotonic() + 10
                while needle not in transcript:
                    if time.monotonic() > deadline:
                        raise AssertionError(name + ': native preference state deadline')
                    ready, _, _ = select.select([master], [], [], .1)
                    if ready:
                        try:
                            data = os.read(master, 65536)
                        except OSError:
                            raise AssertionError(name + ': terminal closed before expected state') from None
                        if not data:
                            raise AssertionError(name + ': terminal EOF')
                        transcript += data

            try:
                until(b'[S]')
                os.write(master, b's')
                if name == 'held-read':
                    until(b'Preferences PENDING')
                else:
                    until(b'CanaryMiner / Settings')
                    os.write(master, b'\t v\r')
                    until(b'Preferences PENDING', True)
                target = state / 'ui-preferences.json'
                if name == 'lost-ack':
                    until(b'Preferences UNKNOWN', True)
                    assert target.is_file() and json.loads(target.read_text())['refresh_ms'] == 1000
                    assert b'hide_worker' not in transcript and b'Saved.' not in transcript
                    checks += 2
                else:
                    assert not target.exists()
                    checks += 1
                began = time.monotonic()
                os.write(master, b'q')
                child.wait(timeout=4)
                assert child.returncode == 0 and time.monotonic() - began < 3
                starts = state / 'fixture-worker-starts.txt'
                if name == 'held-read':
                    assert not starts.exists(), 'initial preference inspection must precede every mining-worker launch'
                else:
                    assert starts.read_text().splitlines() == ['start']
                assert b'FORBIDDEN_DEVICE_SETUP' not in transcript
                checks += 3
                if name != 'lost-ack':
                    assert not target.exists()
                    checks += 1
            finally:
                if child.poll() is None:
                    child.send_signal(signal.SIGTERM)
                    try:
                        child.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        child.kill()
                        child.wait(timeout=3)
                os.close(master)
    print(f'preferences_supervisor_terminal: {checks} checks; held reads/writes and actual lost commit acknowledgement do not block owned fake-worker stop')


if __name__ == '__main__':
    main()
