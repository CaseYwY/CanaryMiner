"""Native POSIX PTY: author a composite declaration, separate save, conflict, active-session and stop behavior."""
import json
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
        raise SystemExit('POSIX PTY only; Windows uses native state/store/renderer checks')
    import fcntl
    import pty
    import termios
    cli, supervisor = [str(Path(p).resolve()) for p in sys.argv[1:3]]
    example = json.loads((Path(__file__).resolve().parents[1] / 'config/tariff-declaration.example.json').read_text())
    # Synthetic publication must be in the past relative to the native confirmation clock.
    shift = (int(time.time()) // 86400 - 1) * 86400000 + 28800000 - 1800000000000
    def shift_instants(value):
        if isinstance(value, dict):
            for name, item in value.items():
                if name.endswith('_utc_ms'):
                    value[name] = item + shift
                else:
                    shift_instants(item)
        elif isinstance(value, list):
            for item in value:
                shift_instants(item)
    shift_instants(example)
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-tariff-terminal-') as directory:
        root = Path(directory)
        state = root / 'state'
        config = root / 'config.json'
        config.write_text(json.dumps({'schema_version': 1, 'state_dir': str(state)}))
        file = state / 'tariff-declaration.json'
        starts = state / 'fixture-worker-starts.txt'
        rejected = subprocess.run([cli, 'settings', '--config', str(config)], capture_output=True, timeout=5)
        assert rejected.returncode == 2 and not file.exists()
        checks += 1

        def session(command, work):
            nonlocal checks
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 40, 120, 0, 0))
            def own_terminal():
                os.setsid()
                fcntl.ioctl(0, termios.TIOCSCTTY, 0)
            child = subprocess.Popen(command, stdin=slave, stdout=slave, stderr=slave, preexec_fn=own_terminal)
            os.close(slave)
            transcript = b''

            def until(needle, fresh=False):
                nonlocal transcript
                if fresh:
                    transcript = b''
                deadline = time.monotonic() + 10
                while needle not in transcript:
                    if time.monotonic() > deadline:
                        raise AssertionError('tariff terminal readiness deadline: ' + needle.decode('ascii'))
                    if select.select([master], [], [], 0.1)[0]:
                        try:
                            data = os.read(master, 65536)
                        except OSError:
                            raise AssertionError('tariff terminal closed before expected state') from None
                        if not data:
                            raise AssertionError('terminal EOF')
                        transcript += data
                return transcript

            def send(data):
                os.write(master, data)

            def tariff():
                send(b'\x1b[C' * 2 + b'\t\x1b[B\r')
                until(b'Combined tariff / declared opening', True)

            try:
                work(send, until, tariff)
                deadline = time.monotonic() + 5
                while child.poll() is None and time.monotonic() < deadline:
                    if select.select([master], [], [], 0.05)[0]:
                        try:
                            transcript += os.read(master, 65536)
                        except OSError:
                            break
                assert child.wait(timeout=1) == 0
                assert b'FORBIDDEN_DEVICE_SETUP' not in transcript
                checks += 1
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait(timeout=5)
                os.close(master)

        # Build the fixture from BLANK native fields, including nested calendar and band tables.
        def author(send, until, tariff):
            nonlocal checks
            until(b'CanaryMiner / Settings')
            send(b'\t ')  # independent unsaved display draft
            send(b'\x1b[C' * 2 + b'\x1b[B\r')
            until(b'Combined tariff / declared opening', True)
            assert not file.exists()
            fixed = {'schema_version', 'confirmed_utc', 'reset_policy', 'rounding', 'basis', 'origin', 'time_database_version'}

            # Standalone Escape is resolved by the native key reader before selecting the next sibling.
            def write_object(value):
                for i, (name, v) in enumerate(value.items()):
                    if i:
                        send(b'\x1b[B')
                    if name in fixed:
                        continue
                    if isinstance(v, bool):
                        if v:
                            send(b' ')
                    elif isinstance(v, dict):
                        send(b'\r')
                        until(b'> ' + next(iter(v)).encode() + b':', True)
                        write_object(v)
                        send(b'\x1b')
                        until(b'> ' + name.encode() + b':', True)
                    elif isinstance(v, list):
                        send(b'\r')
                        until(b'Path:', True)
                        for row_index, row in enumerate(v):
                            if name != 'revisions' or row_index:
                                send(b'a')
                            send(b'\r')
                            until(b'> ' + next(iter(row)).encode() + b':', True)
                            write_object(row)
                            send(b'\x1b')
                            until(b'> row ' + str(row_index).encode(), True)
                        send(b'\x1b')
                        until(b'> ' + name.encode() + b':', True)
                    elif v is not None:
                        send(b'\r' + str(v).encode() + b'\r')

            write_object(example)
            send(b'v')
            until(b'Review tariff-declaration.json ONLY', True)
            assert not file.exists() and not starts.exists()
            send(b'\r')
            until(b'Saved offline declaration + opening ONLY', True)
            saved = json.loads(file.read_text())
            assert saved['plan'] == example['plan'] and saved['opening'] == example['opening']
            assert 0 < saved['confirmed_utc'] <= int(time.time())
            assert not (state / 'ui-preferences.json').exists() and not (state / 'electricity-preferences.json').exists()
            checks += 5
            send(b'\x1b')
            until(b'CanaryMiner / Settings', True)
            send(b'\x1b')
            until(b'Unsaved changes', True)
            send(b'd')

        session([cli, 'settings', '--config', str(config)], author)
        saved_bytes = file.read_bytes()

        def conflict(send, until, tariff):
            nonlocal checks
            until(b'CanaryMiner / Settings')
            tariff()
            send(b'\x1b[B' * 6 + b'\r' + b'\x1b[B' * 8 + b'\r3\rv\r')
            until(b'NEW plan.key', True)
            assert file.read_bytes() == saved_bytes
            checks += 1
            send(b'q')

        session([cli, 'settings', '--config', str(config)], conflict)

        def active(send, until, tariff):
            nonlocal checks
            main = until(b'all local 2')
            assert b'SESSION GPU-BOARD ENERGY' not in main and b'PRODUCTION AND REFERENCE VALUATION' not in main
            send(b's')
            until(b'CanaryMiner / Settings', True)
            tariff()
            send(b'v\r')  # confirmation only, no model/rate mutation
            until(b'Saved offline declaration + opening ONLY', True)
            assert starts.read_text().splitlines() == ['start']
            assert not (state / 'electricity-preferences.json').exists()
            checks += 3
            send(b'\x1b')
            until(b'CanaryMiner / Settings', True)
            send(b'\x1b')
            main = until(b'[S]', True)
            assert b'SESSION GPU-BOARD ENERGY' not in main
            checks += 1
            send(b'q')

        session([supervisor, '--dashboard-session-test', str(state)], active)

        def stop_editor(send, until, tariff):
            until(b'CanaryMiner / Settings')
            tariff()
            send(b'\x1b[B\rqDSL')
            until(b'> qDSL_', True)
            send(b'\x03')

        before = file.read_bytes()
        session([cli, 'settings', '--config', str(config)], stop_editor)
        assert file.read_bytes() == before and not list(state.glob('*credit*'))
        checks += 1
    print(f'tariff declaration PTY: {checks} checks; nested author/review/save/conflict/active-session/stop passed')


if __name__ == '__main__':
    main()
