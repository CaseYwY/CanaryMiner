"""POSIX real owner/parser/native PTY with invented quotes and inert children; no public market, GPU or account I/O."""
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
        raise SystemExit('POSIX PTY only; Windows uses native controller/owner tests')
    import fcntl
    import pty
    import termios
    fixture = str(Path(sys.argv[1]).resolve())
    checks = 0

    def session(root, work, disabled=False):
        nonlocal checks
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 60, 120, 0, 0))

        def own_terminal():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)

        child = subprocess.Popen([fixture, str(root), *(['--no-market'] if disabled else [])], stdin=slave, stdout=slave, stderr=slave, preexec_fn=own_terminal)
        os.close(slave)
        transcript = b''

        def until(needle, fresh=False):
            nonlocal transcript
            if fresh:
                transcript = b''
            deadline = time.monotonic() + 10
            while needle not in transcript:
                if time.monotonic() >= deadline:
                    raise AssertionError('native public-reference terminal readiness deadline')
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
                text = until(b'[S]')
                assert b'PUBLIC MARKET REFERENCES' not in text and b'MANUAL/OFFLINE PRICE REFERENCES' not in text
                if not disabled:
                    until(b'Public and manual market sources both requested')
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
