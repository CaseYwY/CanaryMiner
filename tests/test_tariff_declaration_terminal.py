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

        def compare(send, until, tariff):
            nonlocal checks
            until(b'CanaryMiner / Settings')
            tariff()
            until(b'[P] Compare whole-meter')
            send(b'p')
            until(b'Whole-meter comparison | HYPOTHETICAL', True)
            send(b'0\t1000\r')
            result = until(b'Difference (with - without): 9.020000 USD', True)
            assert b'2.420000 USD' in result and b'11.440000 USD' in result
            assert file.read_bytes() == saved_bytes and not starts.exists()
            checks += 3
            send(b'\rq')  # Queue a second comparison, then stop through the normal input path.

        session([cli, 'settings', '--config', str(config)], compare)

        def conflict(send, until, tariff):
            nonlocal checks
            until(b'CanaryMiner / Settings')
            tariff()
            send(b'\x1b[B' * 6 + b'\r' + b'\x1b[B' * 8 + b'\r3\rv\r')
            refused = until(b'Preferences UNKNOWN', True)
            assert b'NOT Saved' in refused and b'confirmed_utc' not in refused
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
            until(b'[P] Compare whole-meter')
            send(b'p')
            until(b'Whole-meter comparison | HYPOTHETICAL', True)
            send(b'0\t1000\r')
            result = until(b'Difference (with - without): 9.020000 USD', True)
            assert b'2.420000 USD' in result and b'11.440000 USD' in result
            assert file.read_bytes() == saved_bytes and starts.read_text().splitlines() == ['start']
            checks += 3
            send(b'\r\x1b')  # Cancel the replacement calculation and return to the same tariff page.
            until(b'Combined tariff / declared opening', True)
            send(b'p')
            reopened = until(b'[Tab] Field [Enter] Compare', True)
            assert b'Without mining (W):' in reopened
            assert b'9.020000 USD' not in reopened and b'Without mining (W): 0' not in reopened
            assert file.read_bytes() == saved_bytes and starts.read_text().splitlines() == ['start']
            checks += 2
            send(b'\x1b')
            until(b'Combined tariff / declared opening', True)
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

        def stop_dashboard_comparison(send, until, tariff, stop):
            until(b'all local 2')
            send(b's')
            until(b'CanaryMiner / Settings', True)
            tariff()
            send(b'p')
            until(b'Whole-meter comparison | HYPOTHETICAL', True)
            send(b'0\t1000\r')
            until(b'Difference (with - without): 9.020000 USD', True)
            send(b'\r' + stop)

        for name, stop in (('q', b'q'), ('ctrl-c', b'\x03')):
            isolated_state = root / ('stop-comparison-' + name)
            isolated_state.mkdir()
            before = file.read_bytes()
            declaration = isolated_state / file.name
            declaration.write_bytes(before)
            session([supervisor, '--dashboard-session-test', str(isolated_state)],
                    lambda send, until, tariff: stop_dashboard_comparison(send, until, tariff, stop))
            assert declaration.read_bytes() == before
            assert (isolated_state / starts.name).read_text().splitlines() == ['start']
            checks += 2

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

        def monthly(send, until, tariff):
            nonlocal checks
            down = b'\x1b[B'
            until(b'CanaryMiner / Settings')
            tariff()
            send(down * 5)
            until(b'[M] Cycle policy', True)
            send(b'm\r\r31\r')
            send(down + b'\r\rfixed_standard\r' + down + b'\r19800\r')
            send(b'\x1b')
            until(b'> clock:', True)
            send(b'\x1b')
            until(b'> reset_policy:', True)
            send(down * 2 + b'\r' + down * 7 + b'\r1709164800000\rg')
            until(b'Clear both opening amounts', True)
            assert file.read_bytes() == before
            send(b'\r\x7f\r' + down + b'\r\x7f\rg')
            until(b'Draft cycle dates derived', True)
            send(down * 3 + b'\r3\r' + down + b'\r0.6\r')
            send(b'\x1b')
            until(b'> opening:', True)
            send(b'\x1b[A\r\rsynthetic-monthly-plan\r' + down * 11 + b'\r\r')
            until(b'Path: plan.revisions[0]', True)
            send(down + b'\r1709145000000\r' + down + b'\r1711823400000\r')
            send(b'\x1b')
            until(b'> row 0:', True)
            send(b'\x1b')
            until(b'> revisions:', True)
            send(b'\x1b')
            until(b'> plan:', True)
            send(down + b'\r\rsynthetic-monthly-opening\rv')
            until(b'Monthly reset at 00:00', True)
            assert file.read_bytes() == before
            send(b'\r')
            until(b'Saved offline declaration + opening ONLY', True)
            saved = json.loads(file.read_text())
            assert saved['schema_version'] == 2
            assert saved['reset_policy'] == {'monthly_day': 31, 'clock': {'kind': 'fixed_standard', 'fixed_offset_seconds': 19800}}
            assert saved['plan']['clock'] == example['plan']['clock']
            opening = saved['opening']
            assert [opening[name] for name in ('cycle_begin_utc_ms', 'cycle_end_utc_ms', 'through_utc_ms',
                                               'imported_kwh', 'accrued_subtotal')] == [1709145000000, 1711823400000, 1709164800000, '3', '0.6']
            checks += 6
            send(b'q')

        session([cli, 'settings', '--config', str(config)], monthly)
        monthly_bytes = file.read_bytes()

        horizon_state = root / 'monthly-comparison'
        horizon_state.mkdir()
        horizon = json.loads(monthly_bytes)
        horizon['reset_policy']['clock'] = {'kind': 'utc', 'fixed_offset_seconds': 0}
        horizon['plan']['tiers'] = 'none'
        horizon['plan']['revisions'][0].update(
            begin_utc_ms=1709164800000, end_utc_ms=1714435200000, time_mode='constant',
            charge_per_civil_day='0', calendar=[], bands=[])
        horizon['opening'].update(
            cycle_begin_utc_ms=1709164800000, cycle_end_utc_ms=1711843200000,
            through_utc_ms=1711756800000, imported_kwh='21', accrued_subtotal='2')
        horizon_file = horizon_state / file.name
        horizon_original = json.dumps(horizon).encode()
        horizon_file.write_bytes(horizon_original)

        def compare_monthly(send, until, tariff):
            nonlocal checks
            until(b'all local 2')
            send(b's')
            until(b'CanaryMiner / Settings', True)
            tariff()
            send(b'p')
            until(b'Cycles 1/12', True)
            send(b'\x1b[C')
            span = until(b'Cycles 2/12', True)
            assert b'UTC ms 1711756800000 to 1714435200000' in span
            send(b'0\t1000\r')
            amounts = until(b'Difference (with - without): 163.680000 USD', True)
            assert b'2.200000 USD' in amounts and b'165.880000 USD' in amounts
            send(b'\x1b[C\r')
            unavailable = until(b'Comparison unavailable in cycle 3', True)
            assert b'163.680000 USD' not in unavailable
            send(b'\x1b[D\x1b[D\r')
            until(b'Difference (with - without): 5.280000 USD', True)
            assert horizon_file.read_bytes() == horizon_original
            assert (horizon_state / starts.name).read_text().splitlines() == ['start']
            checks += 6
            send(b'q')

        session([supervisor, '--dashboard-session-test', str(horizon_state)], compare_monthly)

        def reopen_monthly(send, until, tariff):
            nonlocal checks
            until(b'CanaryMiner / Settings')
            tariff()
            send(b'g')
            until(b'Draft cycle dates derived', True)
            send(b'\x1b')
            until(b'> opening:', True)
            send(b'\x1b[A' * 2 + b'm')
            until(b'"explicit_cycle_only" [fixed]', True)
            send(b'\x1b')
            until(b'Discard', True)
            send(b'd')
            until(b'CanaryMiner / Settings', True)
            assert file.read_bytes() == monthly_bytes
            checks += 1
            send(b'q')

        session([cli, 'settings', '--config', str(config)], reopen_monthly)

        def revert_monthly(send, until, tariff):
            nonlocal checks
            down = b'\x1b[B'
            until(b'CanaryMiner / Settings')
            tariff()
            send(down * 5 + b'm')
            until(b'"explicit_cycle_only" [fixed]', True)
            send(down + b'\r\rsynthetic-explicit-plan\r\x1b')
            until(b'> plan:', True)
            send(down + b'\r\rsynthetic-explicit-opening\rv')
            until(b'Review tariff-declaration.json ONLY', True)
            assert file.read_bytes() == monthly_bytes
            send(b'\r')
            until(b'Saved offline declaration + opening ONLY', True)
            saved = json.loads(file.read_text())
            assert saved['schema_version'] == 1 and saved['reset_policy'] == 'explicit_cycle_only'
            assert saved['opening'] == {**json.loads(monthly_bytes)['opening'], 'key': 'synthetic-explicit-opening'}
            checks += 3
            send(b'q')

        session([cli, 'settings', '--config', str(config)], revert_monthly)

        def reopen_explicit(send, until, tariff):
            until(b'CanaryMiner / Settings')
            tariff()
            send(b'\x1b[B' * 5)
            restored = until(b'[M] Cycle policy', True)
            assert b'> reset_policy: "explicit_cycle_only" [fixed]' in restored
            assert b'[G] Derive cycle' not in restored
            send(b'q')

        session([cli, 'settings', '--config', str(config)], reopen_explicit)
    print(f'tariff declaration PTY: {checks} checks; author/review/save/comparison/conflict/active-session/stop/monthly policy passed')


if __name__ == '__main__':
    main()
