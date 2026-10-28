"""Actual POSIX native settings/fake supervisor: next-start activation, no fabricated power and separate local targets."""
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
        raise SystemExit('POSIX PTY only; Windows uses native state/store/renderer tests')
    import fcntl
    import pty
    import termios
    cli, supervisor = [str(Path(p).resolve()) for p in sys.argv[1:3]]
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-energy-terminal-') as directory:
        root = Path(directory)
        state = root / 'state'
        config = root / 'config.json'
        config.write_text(json.dumps({'schema_version': 1, 'state_dir': str(state)}), encoding='utf8')
        file = state / 'electricity-preferences.json'
        starts = state / 'fixture-worker-starts.txt'
        rejected = subprocess.run([cli, 'settings', '--no-energy', '--config', str(config)], capture_output=True, timeout=5)
        assert rejected.returncode == 2 and not file.exists()
        checks += 1

        def session(command, work):
            nonlocal checks
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 60, 120, 0, 0))
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
                deadline = time.monotonic() + 8
                while needle not in transcript:
                    if time.monotonic() > deadline:
                        raise AssertionError('session-energy terminal readiness deadline')
                    readable, _, _ = select.select([master], [], [], 0.1)
                    if readable:
                        try:
                            data = os.read(master, 65536)
                        except OSError:
                            raise AssertionError('terminal closed before expected energy state') from None
                        if not data:
                            raise AssertionError('terminal EOF before energy state')
                        transcript += data
                return transcript

            def send(data):
                os.write(master, data)

            def electricity():
                send(b'\x1b[C' * 2 + b'\t\r')
                until(b'Electricity/energy', True)

            try:
                work(send, until, electricity)
                # Continue draining the real terminal until exit; do not let a final frame block the test itself.
                deadline = time.monotonic() + 5
                while child.poll() is None and time.monotonic() < deadline:
                    if select.select([master], [], [], 0.05)[0]:
                        try:
                            transcript += os.read(master, 65536)
                        except OSError:
                            break
                assert child.wait(timeout=1) == 0
                assert child.returncode == 0 and b'FORBIDDEN_DEVICE_SETUP' not in transcript
                checks += 1
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait(timeout=5)
                os.close(master)

        def setup(send, until, _):
            nonlocal checks
            until(b'CanaryMiner / Settings')
            send(b'\t ' + b'\x1b[C' * 2 + b'\r')  # retain an independent unsaved UI draft
            until(b'Electricity/energy', True)
            send(b' \t\t\r500\r' + b'\t\t \t ')
            # Current field is 5 (exclusions). Fill metadata without invoking any source or worker.
            for value in ('synthetic-country', 'synthetic-region', 'synthetic-supplier', 'synthetic-plan', 'synthetic-v1', 'USD', '0'):
                send(b'\t\r' + value.encode() + b'\r')
            send(b'\t\t\tn\t\r' + str(int(time.time()) + 3600).encode() + b'\r')
            until(b'Effective end UTC seconds', True)
            send(b'v')
            until(b'Review electricity-preferences.json ONLY', True)
            assert not file.exists() and not starts.exists()
            send(b'\r')
            until(b'Saved for NEXT EXPLICIT SESSION ONLY', True)
            saved = json.loads(file.read_text())
            assert saved['collect_energy'] and saved['tariff_enabled'] and saved['exclusions_acknowledged']
            assert saved['rate_per_kwh'] == '0' and saved['currency'] == 'USD' and saved['record_interval_ms'] == 500
            assert 0 < saved['effective_from_utc'] < saved['effective_through_utc'] and saved['confirmed_utc'] <= int(time.time())
            assert not (state / 'ui-preferences.json').exists() and not (state / 'runtime-preferences.json').exists()
            assert not (state / 'manual-market-preferences.json').exists() and not starts.exists()
            checks += 5
            send(b'\x1b')
            until(b'CanaryMiner / Settings', True)
            send(b'\x1b')
            until(b'Unsaved changes', True)
            send(b'd')

        session([cli, 'settings', '--config', str(config)], setup)

        def active(send, until, electricity):
            nonlocal checks
            until(b'SESSION GPU-BOARD ENERGY')
            main = until(b'all local 2')
            assert b'N/A (no covered power interval)' in main and b'PRODUCTION AND REFERENCE VALUATION' not in main
            assert starts.read_text().splitlines() == ['start']
            send(b's')
            until(b'CanaryMiner / Settings', True)
            electricity()
            send(b' v\r')
            until(b'Saved for NEXT EXPLICIT SESSION ONLY', True)
            assert not json.loads(file.read_text())['collect_energy']
            until(b'on (frozen session defaults)', True)
            send(b'\x1b')
            until(b'CanaryMiner / Settings', True)
            send(b'\x1b')
            until(b'SESSION GPU-BOARD ENERGY', True)
            assert starts.read_text().splitlines() == ['start']
            checks += 4
            send(b'q')

        session([supervisor, '--dashboard-session-test', str(state)], active)

        def inactive(send, until, electricity):
            nonlocal checks
            main = until(b'all local 4')
            assert b'SESSION GPU-BOARD ENERGY' not in main
            send(b's')
            until(b'CanaryMiner / Settings', True)
            electricity()
            send(b' v\r')
            until(b'Saved for NEXT EXPLICIT SESSION ONLY', True)
            assert json.loads(file.read_text())['collect_energy']
            send(b'\x1b')
            until(b'CanaryMiner / Settings', True)
            send(b'\x1b')
            main = until(b'[S]', True)
            assert b'SESSION GPU-BOARD ENERGY' not in main and len(starts.read_text().splitlines()) == 2
            checks += 3
            send(b'q')

        session([supervisor, '--dashboard-session-test', str(state)], inactive)

        def override(send, until, electricity):
            nonlocal checks
            main = until(b'all local 6')
            assert b'SESSION GPU-BOARD ENERGY' not in main
            send(b's')
            until(b'CanaryMiner / Settings', True)
            electricity()
            until(b'off (--no-energy)', True)
            send(b'\t v\r')  # hide presentation for the next explicit session; collection remains enabled in the file
            until(b'Saved for NEXT EXPLICIT SESSION ONLY', True)
            saved = json.loads(file.read_text())
            assert saved['collect_energy'] and not saved['show_energy']
            checks += 2
            send(b'q')

        session([supervisor, '--no-energy-session-test', str(state)], override)

        def hidden(send, until, electricity):
            nonlocal checks
            main = until(b'all local 8')
            assert b'SESSION GPU-BOARD ENERGY' not in main
            send(b's')
            until(b'CanaryMiner / Settings', True)
            electricity()
            until(b'on (frozen session defaults)', True)  # hiding presentation does not disable background collection
            send(b'\t' * 12 + b'\rq')
            until(b'> q_', True)
            assert len(starts.read_text().splitlines()) == 4
            checks += 2
            send(b'\x03')  # Ctrl+C stops from inside the text editor without implicitly saving

        session([supervisor, '--dashboard-session-test', str(state)], hidden)
        assert not (state / 'ui-preferences.json').exists() and not (state / 'runtime-preferences.json').exists()
        assert not list(state.glob('*credit*'))
        checks += 2
    print(f'session energy PTY: {checks} checks; idle/frozen-current/next-start/CLI/display-only hiding passed')


if __name__ == '__main__':
    main()
