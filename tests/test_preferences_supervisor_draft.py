"""Actual POSIX live Unknown navigation must preserve an unrelated unsaved root UI draft; owned CPU worker only."""
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
        raise SystemExit('POSIX PTY only')
    import fcntl
    import pty
    import termios
    supervisor, helper = [Path(value).resolve() for value in sys.argv[1:3]]
    with tempfile.TemporaryDirectory(prefix='canary-preferences-draft-') as directory:
        root = Path(directory)
        binary = root / 'supervisor'
        shutil.copy2(supervisor, binary)
        shutil.copy2(helper, root / 'canary_preferences_worker')
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

        def until(needle, fresh=False):
            nonlocal transcript
            if fresh:
                transcript = b''
            deadline = time.monotonic() + 8
            while needle not in transcript:
                if time.monotonic() > deadline:
                    raise AssertionError('native preference unrelated-draft state deadline: ' + needle.decode())
                ready, _, _ = select.select([master], [], [], .1)
                if ready:
                    try:
                        data = os.read(master, 65536)
                    except OSError:
                        raise AssertionError('native preference terminal closed') from None
                    if not data:
                        raise AssertionError('native preference terminal EOF')
                    transcript += data

        try:
            until(b'[S]')
            os.write(master, b's')
            until(b'CanaryMiner / Settings', True)
            os.write(master, b'\t\x1b[B \x1b[C\r')  # unsaved Color off; enter Runtime without saving root UI
            until(b'CanaryMiner / Runtime controls', True)
            os.write(master, b'\r23\rv')
            until(b'Review runtime-preferences.json only', True)
            runtime.write_text(original + '\n')  # genuine external-byte conflict; do not restore/retry automatically
            os.write(master, b'\r')
            until(b'Preferences UNKNOWN', True)
            os.write(master, b'\x1b')
            until(b'[S]', True)
            os.write(master, b's')
            until(b'Preferences UNKNOWN', True)
            os.write(master, b'i')
            until(b'ORIGINAL REQUEST | inspected', True)
            os.write(master, b'r')
            until(b'schema_version', True)
            os.write(master, b'\t')
            until(b'CURRENT PARSED VALUES', True)
            os.write(master, b' \r')
            until(b'CanaryMiner / Runtime controls', True)
            os.write(master, b'\x1b')
            until(b'CanaryMiner / Settings', True)
            os.write(master, b'v')
            until(b'Terminal color: on -> off', True)
            assert runtime.read_text() == original + '\n'
            assert not (state / 'ui-preferences.json').exists()
            assert (state / 'fixture-worker-starts.txt').read_text().splitlines() == ['start']
            os.write(master, b'q')
            child.wait(timeout=4)
            assert child.returncode == 0
        finally:
            if child.poll() is None:
                child.send_signal(signal.SIGTERM)
                try:
                    child.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=3)
            os.close(master)
    print('preferences_supervisor_draft: actual Unknown/back/reopen/inspection preserves unrelated UI draft, no retry or worker restart')


if __name__ == '__main__':
    main()
