"""Actual initial preference helper hold must not launch the owned fake mining worker. No GPU/source/miner."""
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
        raise SystemExit('POSIX PTY fixture only')
    import fcntl
    import pty
    import termios
    supervisor, held_read = [Path(value).resolve() for value in sys.argv[1:3]]
    with tempfile.TemporaryDirectory(prefix='canary-preferences-startup-') as directory:
        root = Path(directory)
        binary = root / 'supervisor'
        shutil.copy2(supervisor, binary)
        shutil.copy2(held_read, root / 'canary_preferences_worker')
        state = root / 'state'
        state.mkdir()
        runtime = state / 'runtime-preferences.json'
        original = json.dumps({'schema_version': 1, 'work_wait_ms': 0, 'history_enabled': False})
        runtime.write_text(original)
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 100, 0, 0))
        child = subprocess.Popen([str(binary), '--dashboard-session-test', str(state)], stdin=slave, stdout=slave, stderr=slave, start_new_session=True)
        os.close(slave)
        transcript = b''
        marker = state / 'ui-preferences.json.fixture-load-held'
        def wait(predicate):
            nonlocal transcript
            until = time.monotonic() + 8
            while not predicate():
                assert time.monotonic() < until, 'initial preference phase deadline'
                ready, _, _ = select.select([master], [], [], .05)
                if ready:
                    try:
                        data = os.read(master, 65536)
                    except OSError:
                        raise AssertionError('terminal closed before held initial read') from None
                    assert data, 'terminal EOF before held initial read'
                    transcript += data
        try:
            wait(lambda: b'[S]' in transcript)
            os.write(master, b's')  # During bootstrap this records only an in-memory deferred panel request.
            wait(marker.exists)  # Observed actual inherited-helper Load, not a guessed sleep or queued label.
            assert not (state / 'fixture-worker-starts.txt').exists(), 'worker launched before initial preference decision'
            assert runtime.read_text() == original and not (state / 'ui-preferences.json').exists()
            began = time.monotonic()
            os.write(master, b'q')
            child.wait(timeout=4)
            assert child.returncode == 0 and time.monotonic() - began < 3
            assert not (state / 'fixture-worker-starts.txt').exists(), 'late worker launch after early stop'
            assert b'FORBIDDEN_DEVICE_SETUP' not in transcript
        finally:
            if child.poll() is None:
                child.send_signal(signal.SIGTERM)
                try:
                    child.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=3)
            os.close(master)
    print('preferences_startup_terminal: held initial read + deferred S + early Q; zero fake worker starts, files preserved')


if __name__ == '__main__':
    main()
