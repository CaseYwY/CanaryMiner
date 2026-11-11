"""Actual idle CLI + inherited helper holds/commits, synthetic owned files only; no mining or device setup."""
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
        raise SystemExit('POSIX PTY only; Windows checks the native bridge and compiled idle consumers')
    import fcntl
    import pty
    import termios
    cli, held_read, held_write, lost_ack, ordinary = [Path(p).resolve() for p in sys.argv[1:6]]
    cases = (('read-q', held_read), ('read-signal', held_read), ('write-q', held_write),
             ('lost-ack', lost_ack), ('missing-helper', None), ('malformed-ui', ordinary))
    for name, helper in cases:
        with tempfile.TemporaryDirectory(prefix='canary-preferences-idle-') as directory:
            root = Path(directory)
            binary = root / 'idle-cli'
            shutil.copy2(cli, binary)
            if helper:
                shutil.copy2(helper, root / 'canary_preferences_worker')
            state = root / 'state'
            state.mkdir()
            config = root / 'config.json'
            original_config = json.dumps({'schema_version': 1, 'state_dir': str(state)})
            config.write_text(original_config)
            target = state / 'ui-preferences.json'
            invalid = '{"schema_version":99,"private":"fixture.private-original-must-stay-private"}'
            if name == 'malformed-ui':
                target.write_text(invalid)
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 100, 0, 0))
            child = subprocess.Popen([str(binary), 'settings', '--config', str(config)], stdin=slave,
                                     stdout=slave, stderr=slave, start_new_session=True)
            os.close(slave)
            transcript = b''

            def wait(predicate):
                nonlocal transcript
                deadline = time.monotonic() + 8
                while not predicate():
                    assert time.monotonic() < deadline, name + ': actual idle preference phase deadline'
                    ready, _, _ = select.select([master], [], [], .03)
                    if ready:
                        try:
                            chunk = os.read(master, 65536)
                        except OSError:
                            assert predicate(), name + ': terminal closed before expected state'
                            break
                        assert chunk, name + ': unexpected terminal EOF'
                        transcript += chunk

            try:
                if name in ('missing-helper', 'malformed-ui'):
                    wait(lambda: child.poll() is not None)
                    assert child.returncode == 3
                    assert (b'Startup configuration unavailable' if name == 'missing-helper' else b'Idle preference inspection refused') in transcript
                    assert target.read_text() == invalid if name == 'malformed-ui' else not target.exists()
                    assert b'fixture.private-original-must-stay-private' not in transcript
                else:
                    if name.startswith('read-'):
                        marker = state / 'ui-preferences.json.fixture-load-held'
                        wait(lambda: marker.exists() and b'Preferences PENDING' in transcript)
                        assert b'CanaryMiner / Settings' not in transcript and not target.exists()
                    else:
                        wait(lambda: b'CanaryMiner / Settings' in transcript)
                        transcript = b''
                        os.write(master, b'\t v\r')
                        if name == 'write-q':
                            marker = state / 'ui-preferences.json.fixture-save-held'
                            wait(lambda: marker.exists() and b'Preferences PENDING' in transcript)
                            assert not target.exists()
                        else:
                            wait(lambda: b'Preferences UNKNOWN' in transcript)
                            assert target.is_file() and json.loads(target.read_text())['refresh_ms'] == 1000
                            assert b'hide_worker' not in transcript
                        assert b'Saved.' not in transcript
                    began = time.monotonic()
                    if name == 'read-signal':
                        child.send_signal(signal.SIGINT)
                    else:
                        os.write(master, b'q')
                    child.wait(timeout=4)
                    assert child.returncode == 0 and time.monotonic() - began < 3
                    if name != 'lost-ack':
                        assert not target.exists(), name + ': a held read/write must not create a preference'
                assert config.read_text() == original_config
                assert b'FORBIDDEN_DEVICE_SETUP' not in transcript
                assert not (state / 'fixture-worker-starts.txt').exists()
                for other in ('runtime-preferences.json', 'game-preferences.json', 'pool-security-preferences.json',
                              'language-preferences.json', 'manual-market-preferences.json',
                              'electricity-preferences.json', 'tariff-declaration.json'):
                    assert not (state / other).exists(), name + ': unrelated preference was written'
            finally:
                if child.poll() is None:
                    child.send_signal(signal.SIGTERM)
                    try:
                        child.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        child.kill()
                        child.wait(timeout=3)
                os.close(master)
    print('preferences_idle_terminal: six actual idle held-read/signal/held-write/lost-ack/refusal cases; no worker, no speculative Saved or fallback')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    main()
