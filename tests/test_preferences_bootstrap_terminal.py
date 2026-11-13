"""Explicit helper phase releases prove serial bootstrap and deferred Settings ticket ownership; CPU-only fake worker."""
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
    supervisor, helper = [Path(value).resolve() for value in sys.argv[1:3]]
    with tempfile.TemporaryDirectory(prefix='canary-preferences-bootstrap-') as directory:
        root = Path(directory)
        binary = root / 'supervisor'
        shutil.copy2(supervisor, binary)
        shutil.copy2(helper, root / 'canary_preferences_worker')
        state = root / 'state'
        state.mkdir()
        original = json.dumps({'schema_version': 1, 'work_wait_ms': 0, 'history_enabled': False})
        (state / 'runtime-preferences.json').write_text(original)
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 100, 0, 0))
        child = subprocess.Popen([str(binary), '--dashboard-session-test', str(state)], stdin=slave, stdout=slave, stderr=slave, start_new_session=True)
        os.close(slave)
        transcript = b''
        def wait(predicate):
            nonlocal transcript
            deadline = time.monotonic() + 4
            while not predicate():
                assert time.monotonic() < deadline, 'explicit bootstrap phase deadline'
                ready, _, _ = select.select([master], [], [], .02)
                if ready:
                    try:
                        data = os.read(master, 65536)
                    except OSError:
                        raise AssertionError('bootstrap terminal closed') from None
                    assert data, 'bootstrap terminal EOF'
                    transcript += data
        log = state / 'fixture-preference-loads.txt'
        starts = state / 'fixture-worker-starts.txt'
        def loads():
            return log.read_text().splitlines() if log.exists() else []
        try:
            wait(lambda: loads() == ['2:ui'])
            assert not starts.exists()
            os.write(master, b's')
            wait(lambda: b'Settings requested' in transcript)
            for ticket, expected in ((2, ['2:ui', '3:manual']), (3, ['2:ui', '3:manual', '4:electricity'])):
                (state / ('fixture-preference-release-' + str(ticket))).touch()
                wait(lambda: loads() == expected)
                assert not starts.exists(), 'worker launched before all initial preferences settled'
            (state / 'fixture-preference-release-4').touch()
            wait(lambda: loads() == ['2:ui', '3:manual', '4:electricity', '5:ui'])
            # The worker can create the append file before its first record is visible.
            wait(lambda: starts.exists() and starts.read_text().endswith('\n'))
            assert starts.read_text().splitlines() == ['start']
            (state / 'fixture-preference-release-5').touch()
            transcript = b''
            wait(lambda: b'CanaryMiner / Settings' in transcript)
            assert b'Saved.' not in transcript and b'FORBIDDEN_DEVICE_SETUP' not in transcript
            assert (state / 'runtime-preferences.json').read_text() == original
            for name in ('ui-preferences.json', 'manual-market-preferences.json', 'electricity-preferences.json'):
                assert not (state / name).exists(), 'initial inspection must not write a missing preference'
            os.write(master, b'q')
            child.wait(timeout=4)
            assert child.returncode == 0 and starts.read_text().splitlines() == ['start']
        finally:
            if child.poll() is None:
                child.send_signal(signal.SIGTERM)
                try:
                    child.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=3)
            os.close(master)
    print('preferences_bootstrap_terminal: exact UI/manual/electricity tickets, delayed S owns a fresh UI ticket; one worker, no preference writes')


if __name__ == '__main__':
    main()
