"""POSIX real owner/parser/native PTY with invented quotes and inert children; no public market, GPU or account I/O."""
import json
import os
from pathlib import Path
import select
import shutil
import struct
import subprocess
import sys
import tempfile
import time

from test_market_trade_review_cli import gate_body, gate_row, kraken_body


def main():
    if os.name != 'posix':
        raise SystemExit('POSIX PTY only; Windows uses native controller/owner tests')
    import fcntl
    import pty
    import termios
    fixture = Path(sys.argv[1]).resolve()
    helper_fixture = Path(sys.argv[2]).resolve()
    preference_fixture = Path(sys.argv[3]).resolve()
    checks = 0

    def session(root, work, disabled=False, hour=False, resolver=None, trades=False, fallback_mode=None):
        nonlocal checks
        executable = fixture
        if hour:
            installed = root / 'installed'
            installed.mkdir(parents=True)
            executable = Path(shutil.copy2(fixture, installed / fixture.name))
            shutil.copy2(helper_fixture, installed / 'canary_public_json_worker')
            shutil.copy2(preference_fixture, installed / 'canary_preferences_worker')
        if trades:
            now = int(time.time())
            (installed / 'trade-response-7.json').write_bytes(gate_body(gate_row(now - 3)))
            (installed / 'trade-response-8.json').write_bytes(kraken_body(('0.98', f'{now - 4}.25', 4)))
        if fallback_mode:
            (installed / 'candle-scenario').write_text(fallback_mode, encoding='ascii')
        options = ['--kraken-hour-fixture'] if fallback_mode else ['--trade-hour-fixture'] if trades else (
            ['--hour-fixture'] if hour else ['--no-market'] if disabled else [])
        if resolver:
            assert hour
            options.append(resolver)
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 60, 120, 0, 0))

        def own_terminal():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)

        child = subprocess.Popen([executable, str(root), *options], stdin=slave, stdout=slave, stderr=slave, preexec_fn=own_terminal)
        os.close(slave)
        transcript = b''

        def until(needle, fresh=False):
            nonlocal transcript
            if fresh:
                transcript = b''
            deadline = time.monotonic() + 10
            while needle not in transcript:
                if time.monotonic() >= deadline:
                    raise AssertionError(f'native public-reference terminal did not show {needle!r}: {transcript[-3000:]!r}')
                readable, _, _ = select.select([master], [], [], 0.1)
                if readable:
                    try:
                        data = os.read(master, 65536)
                    except OSError:
                        raise AssertionError('native public-reference terminal closed early') from None
                    if not data:
                        raise AssertionError('native public-reference terminal EOF')
                    transcript += data
            return transcript

        def send(data):
            os.write(master, data)

        try:
            # The pending startup screen also has [S]; it is deliberately available before any worker.
            # Observe this fixture's real child admission instead of racing Q against that early screen.
            deadline = time.monotonic() + 10
            while not (root / 'worker-starts').exists():
                assert child.poll() is None and time.monotonic() < deadline, 'owned synthetic worker admission'
                if select.select([master], [], [], .05)[0]:
                    try:
                        transcript += os.read(master, 65536)
                    except OSError as error:
                        raise AssertionError('native fixture closed before worker admission') from error
            work(send, until)
            child.wait(timeout=8)
            assert child.returncode == 0
            counts = json.loads((root / 'result.json').read_text())
            assert not counts['late']
            assert (root / 'worker-starts').read_text().splitlines() == ['start']
            assert not (root / 'ui-preferences.json').exists() and not (root / 'runtime-preferences.json').exists()
            checks += 4
            return counts
        finally:
            if child.poll() is None:
                child.kill()
                child.wait(timeout=8)
            os.close(master)

    with tempfile.TemporaryDirectory(prefix='canary-public-reference-') as temporary:
        root = Path(temporary)
        active = root / 'active'

        def live(send, until):
            nonlocal checks
            text = until(b'Latest PRL/USD reference: 2.45')
            assert b'SYNTHETIC FIXTURE - NOT LIVE' in text
            assert b'PRODUCTION AND REFERENCE VALUATION' not in text and b'Estimated net' not in text
            until(b'trade-event time NOT SUPPLIED')
            assert not (active / 'manual-market-preferences.json').exists()
            send(b's')
            until(b'CanaryMiner / Settings', True)
            send(b'\x1b[C' * 3 + b'\t\r')
            until(b'Manual/offline market', True)
            send(b' \t\r2.5\r\tnv\r')
            until(b'Public references requested for this parent', True)
            assert not (active / 'manual-market-preferences.json').exists()
            assert (active / 'worker-starts').read_text().splitlines() == ['start']
            checks += 5
            # Owning controlling terminal: the byte produces a real signal, not a swallowed control byte.
            send(b'\x03')

        counts = session(active, live)
        assert counts['factories'] == counts['starts'] == counts['joins'] == 1 and counts['fetches'] == 2 and counts['views'] > 0
        checks += 1
        libc = os.confstr('CS_GNU_LIBC_VERSION') if 'CS_GNU_LIBC_VERSION' in os.confstr_names else None
        if libc and tuple(map(int, libc.split()[1].split('.')[:2])) >= (2, 40):
            resolver = '192.168.50.53'

            def public_hour(send, until):
                nonlocal checks
                text = until(b'1h mean of minute closes: 3.000000 USDT/PRL | 3.500000 USD/PRL')
                assert b'Latest PRL/USD reference: 2.45' in text
                assert b'PRODUCTION AND REFERENCE VALUATION' not in text and b'Estimated net' not in text
                assert resolver.encode('ascii') not in text
                until(b'TWAP approximation | candidate data, finality unverified')
                until(b'MEXC PEARLUSDT: 60/60')
                until(b'Coinbase USDT-USD: 60/60')
                until(b'1h UTC seconds [')
                checks += 6
                send(b'q')

            state = root / 'hour'
            counts = session(state, public_hour, hour=True, resolver=resolver)
            payloads = (state / 'installed' / 'candle-requests').read_text().splitlines()
            assert len(payloads) == 2 and payloads[0].startswith('4 ') and payloads[1] == '5 ' + payloads[0][2:]
            assert (state / 'installed' / 'ticker-requests').read_text().splitlines() == ['0', '2']
            assert (state / 'installed' / 'resolver-requests').read_text().splitlines() == [resolver] * 4
            assert counts['factories'] == counts['starts'] == counts['joins'] == 1 and counts['views'] > 0
            checks += 4

            def public_fallback(send, until):
                nonlocal checks
                text = until(b'3.000000 USDT/PRL | 8.000000 USD/PRL (Kraken fallback)')
                assert b'Coinbase USDT-USD: 59/60' in text and resolver.encode('ascii') not in text
                until(b'Kraken USDT/USD fallback: 60/60')
                checks += 3
                send(b'q')

            state = root / 'kraken-hour'
            counts = session(state, public_fallback, hour=True, resolver=resolver, fallback_mode='gap-fx')
            payloads = (state / 'installed' / 'candle-requests').read_text().splitlines()
            assert [line.split()[0] for line in payloads] == ['4', '5', '3']
            assert len({line.split(' ', 1)[1] for line in payloads}) == 1
            assert (state / 'installed' / 'resolver-requests').read_text().splitlines() == [resolver] * 5
            assert counts['factories'] == counts['starts'] == counts['joins'] == 1
            checks += 4

            slow = root / 'kraken-hour-stop'

            def stop_fallback(send, until):
                nonlocal checks
                until(b'Coinbase USDT-USD: 59/60')
                requests = slow / 'installed' / 'candle-requests'
                deadline = time.monotonic() + 3
                while not any(line.startswith('3 ') for line in requests.read_text().splitlines()):
                    assert time.monotonic() < deadline, 'Kraken fallback did not start'
                    time.sleep(.01)
                text = until(b'Kraken USDT/USD fallback:')
                assert b'8.000000 USD/PRL' not in text
                checks += 2
                send(b'q')

            counts = session(slow, stop_fallback, hour=True, resolver=resolver, fallback_mode='slow-kraken')
            assert counts['factories'] == counts['starts'] == counts['joins'] == 1
            assert [line.split()[0] for line in
                    (slow / 'installed' / 'candle-requests').read_text().splitlines()] == ['4', '5', '3']
            checks += 2

            def live_trades(send, until):
                nonlocal checks
                text = until(b'PRL/USD trade reference: 2.45')
                assert b'Gate PRL/USDT trade: 2.5' in text and b'Kraken USDT/USD trade: 0.98' in text
                assert b'Event UTC ' in text and b'Receipt UTC ms ' in text
                text = until(b'1h mean of minute closes: 3.000000 USDT/PRL | 3.500000 USD/PRL')
                assert b'Latest PRL/USD reference' not in text and b'Estimated net' not in text
                assert resolver.encode('ascii') not in text
                checks += 4
                send(b'q')

            state = root / 'trades'
            counts = session(state, live_trades, hour=True, resolver=resolver, trades=True)
            assert (state / 'installed' / 'trade-requests').read_text().splitlines() == ['7', '8']
            assert not (state / 'installed' / 'ticker-requests').exists()
            assert (state / 'installed' / 'resolver-requests').read_text().splitlines() == [resolver] * 4
            assert counts['factories'] == counts['starts'] == counts['joins'] == 1 and counts['views'] > 0
            checks += 4

        manual = {
            'schema_version': 1, 'asset': 'pearl-mainnet-native-prl', 'token_quote': 'USDT', 'fx_base': 'USDT', 'fx_quote': 'USD',
            'enabled': True, 'fx_enabled': False, 'prl_usdt': '3', 'prl_as_of_utc': int(time.time()), 'usdt_usd': '', 'fx_as_of_utc': 0,
            'confirmed_utc': int(time.time()), 'validity_seconds': 3600, 'max_fx_skew_seconds': 30,
        }
        for disabled in (False, True):
            state = root / ('disabled' if disabled else 'conflict')
            state.mkdir()
            file = state / 'manual-market-preferences.json'
            original = json.dumps(manual).encode()
            file.write_bytes(original)

            def withheld(send, until):
                nonlocal checks
                text = until(b'Current ' if disabled else b'Public and manual market sources both requested')
                assert b'PUBLIC MARKET REFERENCES' not in text and b'MANUAL/OFFLINE PRICE REFERENCES' not in text
                assert file.read_bytes() == original
                checks += 2
                send(b'q')

            counts = session(state, withheld, disabled)
            assert counts['factories'] == counts['starts'] == counts['joins'] == counts['fetches'] == counts['views'] == 0
            assert file.read_bytes() == original
            checks += 2
    print(f'public-reference PTY: {checks} checks; actual owner/parser, source conflict, manual-save refusal, CLI suppression and acknowledged SIGINT; synthetic only')


if __name__ == '__main__':
    main()
