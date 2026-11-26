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

    def session(root, work, disabled=False, hour=False, resolver=None):
        nonlocal checks
        executable = fixture
        if hour:
            installed = root / 'installed'
            installed.mkdir(parents=True)
            executable = Path(shutil.copy2(fixture, installed / fixture.name))
            shutil.copy2(helper_fixture, installed / 'canary_public_json_worker')
            shutil.copy2(preference_fixture, installed / 'canary_preferences_worker')
        options = ['--hour-fixture'] if hour else ['--no-market'] if disabled else []
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
