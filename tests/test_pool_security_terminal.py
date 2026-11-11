"""Actual POSIX PTY editor and synthetic supervisor; never a public pool or production worker."""
import json
import os
from pathlib import Path
import select
import signal
import struct
import subprocess
import sys
import tempfile
import time


def main():
    if os.name != 'posix':
        raise SystemExit('POSIX PTY only; Windows has native state/store/host-CLI checks')
    import fcntl
    import pty
    import termios
    executable, supervisor = [str(Path(p).resolve()) for p in sys.argv[1:3]]
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-pool-security-pty-') as directory:
        root = Path(directory)
        state = root / 'state'
        config = root / 'config.json'
        config.write_text(json.dumps({'schema_version': 1, 'state_dir': str(state)}), encoding='utf-8')
        preferences = state / 'pool-security-preferences.json'
        class Session:
            def __init__(self, running=False):
                self.master, slave = pty.openpty()
                fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 100, 0, 0))
                command = [supervisor, '--dashboard-session-test', str(state)] if running else [executable, 'settings', '--config', str(config)]
                self.process = subprocess.Popen(command, stdin=slave, stdout=slave, stderr=slave, start_new_session=True)
                os.close(slave)
                self.transcript = b''
                self.running = running
            def __enter__(self):
                if self.running:
                    self.until(b'[S]')
                    self.send(b's')
                self.until(b'CanaryMiner / Settings')
                self.send(b'\x1b[C' * 6 + b'\t\r')
                self.until(b'Pools / TLS declarations')
                return self
            def __exit__(self, *_):
                if self.process.poll() is None:
                    self.process.kill()
                self.process.wait(timeout=5)
                os.close(self.master)
            def send(self, text):
                self.transcript = b''
                os.write(self.master, text)
            def until(self, text):
                deadline = time.monotonic() + 8
                while text not in self.transcript:
                    if time.monotonic() >= deadline:
                        raise AssertionError('pool security terminal readiness deadline exceeded')
                    ready, _, _ = select.select([self.master], [], [], 0.1)
                    if ready:
                        try:
                            chunk = os.read(self.master, 65536)
                        except OSError:
                            raise AssertionError('pool security terminal closed early') from None
                        assert chunk, 'pool security terminal EOF'
                        self.transcript += chunk
                return self.transcript
            def finished(self):
                self.process.wait(timeout=7)
                assert self.process.returncode == 0
        with Session() as session:
            session.send(b'\t \t\rstratum+ssl://pool-fixture.test:8048\rv')
            session.until(b'Review pool-security-preferences.json only')
            assert not preferences.exists()
            checks += 1
            session.send(b'\r')
            session.until(b'Saved declarations only')
            value = json.loads(preferences.read_text())
            assert value['user_pools'][0]['mode'] == 'strict_tls' and not value['user_pools'][0]['plaintext_consent']
            assert value['donation'] == {'mode': 'unchanged', 'tls_url': '', 'plaintext_url': '', 'plaintext_consent': False}
            assert not (state / 'runtime-preferences.json').exists() and not (state / 'ui-preferences.json').exists() and not (state / 'accepted-history.sqlite3').exists()
            checks += 3
            session.send(b'q')
            session.finished()
        original = preferences.read_bytes()
        with Session() as session:
            session.send(b'\t\t\t\rstratum+tcp://discard-fixture.test:1234\r\x1b')
            session.until(b'Unsaved pool declarations')
            session.send(b'd')
            session.until(b'CanaryMiner / Settings')
            session.send(b'q')
            session.finished()
            assert preferences.read_bytes() == original
            checks += 1
        with Session() as session:
            session.send(b'\t v')
            session.until(b'Review pool-security-preferences.json only')
            value['user_pools'][0]['plaintext_url'] = 'stratum+tcp://conflict-fixture.test:1234'
            preferences.write_text(json.dumps(value), encoding='utf-8')
            session.send(b'\r')
            refused = session.until(b'Preferences UNKNOWN')
            assert b'NOT Saved' in refused and b'conflict-fixture.test' not in refused
            session.process.send_signal(signal.SIGINT)
            session.finished()
            assert json.loads(preferences.read_text()) == value
            checks += 1
        with Session() as session:
            fcntl.ioctl(session.master, termios.TIOCSWINSZ, struct.pack('HHHH', 12, 20, 0, 0))
            session.send(b'v' + b'\x1b[6~' * 100)
            session.until(b'[End of TLS review]')
            session.process.send_signal(signal.SIGINT)
            session.finished()
            checks += 1
        with Session(running=True) as session:
            starts = (state / 'fixture-worker-starts.txt').read_bytes()
            session.send(b'\t v\r')
            session.until(b'Saved declarations only')
            assert (state / 'fixture-worker-starts.txt').read_bytes() == starts
            checks += 1
            session.send(b'\x1b')
            session.until(b'CanaryMiner / Settings')
            session.send(b'\x1b')
            session.until(b'[S]')
            session.send(b'q')
            session.finished()
            checks += 1
    print(f'pool security PTY: {checks} review/save/discard/conflict/resize/frozen-worker checks passed')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    main()
