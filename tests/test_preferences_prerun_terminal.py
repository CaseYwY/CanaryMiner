"""Actual inert CLI + exact sibling helper receipts before any device/source setup; synthetic owned files only."""
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
        raise SystemExit('POSIX PTY only; this requires the CPU-only CLI stub, never a miner binary')
    import fcntl
    import pty
    import termios
    cli, controlled, ordinary = [Path(value).resolve() for value in sys.argv[1:4]]
    kinds = ('language', 'runtime', 'game', 'pool')
    checks = 0

    def scenario(stop_phase=None, use_signal=False, settings=False):
        nonlocal checks
        with tempfile.TemporaryDirectory(prefix='canary-preferences-prerun-') as directory:
            root = Path(directory)
            binary = root/'prerun-cli'
            shutil.copy2(cli, binary)
            shutil.copy2(controlled, root/'canary_preferences_worker')
            state = root/'state'
            state.mkdir()
            original = b'fixture.unrelated-private-original'
            (state/'unrelated.txt').write_bytes(original)
            config = root/'config.json'
            config_bytes = json.dumps({'schema_version': 1, 'state_dir': str(state)}).encode()
            config.write_bytes(config_bytes)
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 100, 0, 0))

            def own_terminal():
                os.setsid()
                fcntl.ioctl(0, termios.TIOCSCTTY, 0)

            command = [str(binary), *(['settings'] if settings else []), '--config', str(config)]
            child = subprocess.Popen(command, stdin=slave, stdout=slave, stderr=slave, preexec_fn=own_terminal)
            os.close(slave)
            transcript = b''
            marker = state/'fixture-preference-loads.txt'

            def loads():
                return marker.read_text().splitlines() if marker.exists() else []

            def until(predicate):
                nonlocal transcript
                deadline = time.monotonic()+8
                while not predicate():
                    assert child.poll() is None, 'pre-run entry bypassed the actual held helper before device setup'
                    assert time.monotonic() < deadline, 'exact pre-run preference receipt deadline'
                    if select.select([master], [], [], .03)[0]:
                        try:
                            transcript += os.read(master, 65536)
                        except OSError:
                            assert predicate(), 'pre-run terminal closed before its expected receipt'

            expected = []
            try:
                for phase, kind in enumerate(kinds[:1] if settings else kinds):
                    ticket = phase+2
                    expected.append(str(ticket)+':'+kind)
                    until(lambda: loads() == expected and b'Pre-run preferences PENDING' in transcript)
                    assert b'FORBIDDEN_DEVICE_SETUP' not in transcript
                    assert not any(state.glob('*preferences.json')) and not (state/'fixture-worker-starts.txt').exists()
                    if phase == stop_phase:
                        began = time.monotonic()
                        if use_signal:
                            child.send_signal(signal.SIGINT)
                        else:
                            os.write(master, b'q')
                        child.wait(timeout=4)
                        assert child.returncode == 0 and time.monotonic()-began < 3
                        assert loads() == expected, 'stop must not issue the next preference Load'
                        break
                    (state/('fixture-preference-release-'+str(ticket))).write_text('release')
                else:
                    assert not settings
                    until(lambda: child.poll() is not None)
                    assert child.returncode == 97 and b'FORBIDDEN_DEVICE_SETUP' in transcript
                assert config.read_bytes() == config_bytes and (state/'unrelated.txt').read_bytes() == original
                assert b'fixture.unrelated-private-original' not in transcript
                assert not any(state.glob('*preferences.json')) and not (state/'tariff-declaration.json').exists()
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

    for phase in range(4):
        for use_signal in (False, True):
            scenario(phase, use_signal)
    scenario()
    for use_signal in (False, True):
        scenario(0, use_signal, settings=True)

    with tempfile.TemporaryDirectory(prefix='canary-preferences-prerun-') as directory:
        root = Path(directory)
        binary = root/'prerun-cli'
        shutil.copy2(cli, binary)
        state = root/'state'
        state.mkdir()
        config = root/'config.json'
        config.write_text(json.dumps({'schema_version': 1, 'state_dir': str(state)}))
        command = [str(binary), '--config', str(config)]
        result = subprocess.run(command, capture_output=True, timeout=8)
        assert result.returncode == 3 and b'Pre-run preferences unavailable' in result.stderr
        assert b'FORBIDDEN_DEVICE_SETUP' not in result.stdout+result.stderr and not list(state.iterdir())
        checks += 1
        shutil.copy2(ordinary, root/'canary_preferences_worker')
        for kind, name in (('runtime preferences', 'runtime-preferences.json'), ('game preferences', 'game-preferences.json'),
                           ('pool security declarations', 'pool-security-preferences.json'), ('language', 'language-preferences.json')):
            path = state/name
            original = b'{"schema_version":99,"fixture.private":"must-not-be-output"}'
            path.write_bytes(original)
            result = subprocess.run(command, capture_output=True, timeout=8)
            text = result.stdout+result.stderr
            if kind == 'language':
                assert result.returncode == 97 and b'legacy English presentation retained' in text
            else:
                assert result.returncode == 3 and kind.encode()+b' unavailable' in text
                assert b'FORBIDDEN_DEVICE_SETUP' not in text
            assert b'must-not-be-output' not in text and path.read_bytes() == original
            assert list(state.iterdir()) == [path], 'read-only admission must not create defaults or stores'
            path.unlink()
            checks += 1
        result = subprocess.run([str(binary), 'settings', '--config', str(config)], capture_output=True, timeout=8)
        assert result.returncode == 2 and b'requires an interactive terminal' in result.stderr and not list(state.iterdir())
        checks += 1
        result = subprocess.run([*command, '--bench'], capture_output=True, timeout=8)
        assert result.returncode == 97 and b'FORBIDDEN_DEVICE_SETUP' in result.stderr and not list(state.iterdir())
        checks += 1
    print('preferences_prerun_terminal:', checks, 'actual ordered-read/early-stop/signal/fallback/refusal/bypass cases; no device backend or preference writes')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    main()
