"""POSIX native settings and synthetic supervisor PTYs; no public pool or production worker."""
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
        raise SystemExit('POSIX PTY only; Windows has native state/store and actual host-CLI checks')
    import fcntl
    import pty
    import termios
    executable, supervisor = [str(Path(p).resolve()) for p in sys.argv[1:3]]
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-language-pty-') as directory:
        root = Path(directory)
        state = root / 'state'
        config = root / 'config.json'
        config.write_text(json.dumps({'schema_version': 1, 'state_dir': str(state)}), encoding='utf-8')
        preferences = state / 'language-preferences.json'
        class Session:
            def __init__(self, running=False, overrides=False):
                self.master, slave = pty.openpty()
                fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 100, 0, 0))
                command = [supervisor, '--dashboard-session-test', str(state)] if running else [executable, 'settings', '--config', str(config)]
                if overrides and not running:
                    command += ['--language=ar', '--format-locale=de-DE', '--ascii-ui']
                self.process = subprocess.Popen(command, stdin=slave, stdout=slave, stderr=slave, start_new_session=True)
                os.close(slave)
                self.transcript = b''
                self.running = running
            def __enter__(self):
                if self.running:
                    self.until(b'[S]')
                    self.send(b's')
                self.until(b'CanaryMiner / Settings')
                self.send(b'\x1b[C' * 7 + b'\t\r')
                self.until(b'Language / format requests')
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
                        raise AssertionError('language terminal readiness deadline exceeded for ' + repr(text))
                    ready, _, _ = select.select([self.master], [], [], 0.1)
                    if ready:
                        try:
                            chunk = os.read(self.master, 65536)
                        except OSError:
                            raise AssertionError('language terminal closed early') from None
                        assert chunk, 'language terminal EOF'
                        self.transcript += chunk
                return self.transcript
            def finished(self):
                self.process.wait(timeout=7)
                assert self.process.returncode == 0
        with Session(overrides=True) as session:
            assert not preferences.exists()
            session.send(b' \t v')  # only the saved draft changes; CLI values are never copied into it
            transcript = session.until(b'Current request: ar')
            assert b'Before: en' in transcript and b'After: zh-Hans' in transcript
            assert not preferences.exists()
            checks += 3
            session.send(b'\r')
            session.until(b'Saved requests only')
            value = json.loads(preferences.read_text())
            assert value['language'] == 'zh-Hans' and value['formatting_locale'] == 'zh-Hans' and value['text_order'] == 'ascii'
            assert value['digits'] == 'latin' and value['direction'] == 'auto'
            assert not (state / 'runtime-preferences.json').exists() and not (state / 'ui-preferences.json').exists()
            checks += 3
            session.send(b'v')
            transcript = session.until(b'Current request: ar')
            assert b'Before: zh-Hans' in transcript and b'After: zh-Hans' in transcript
            checks += 1
            session.process.send_signal(signal.SIGINT)
            session.finished()
        original = preferences.read_bytes()
        with Session() as session:
            session.send(b' \x1b')
            session.until(b'Unsaved language requests')
            session.send(b'd')
            session.until(b'CanaryMiner / Settings')
            session.send(b'q')
            session.finished()
            assert preferences.read_bytes() == original
            checks += 1
        with Session() as session:
            session.send(b' v')
            session.until(b'Review language-preferences.json only')
            value['grouping'] = False
            preferences.write_text(json.dumps(value), encoding='utf-8')
            session.send(b'\r')
            refused = session.until(b'Preferences UNKNOWN')
            assert b'NOT Saved' in refused and b'formatting_locale' not in refused
            session.process.send_signal(signal.SIGINT)
            session.finished()
            assert json.loads(preferences.read_text()) == value
            checks += 1
        with Session() as session:
            fcntl.ioctl(session.master, termios.TIOCSWINSZ, struct.pack('HHHH', 12, 20, 0, 0))
            session.send(b'v' + b'\x1b[6~' * 100)
            session.until(b'[End language]')
            fcntl.ioctl(session.master, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 100, 0, 0))
            session.send(b'\x1b[6~')
            session.until(b'[End language]')
            session.process.send_signal(signal.SIGINT)
            session.finished()
            checks += 2
        with Session(running=True) as session:
            starts = (state / 'fixture-worker-starts.txt').read_bytes()
            session.send(b' v')
            session.until(b'Current request: en')
            session.send(b'\r')
            session.until(b'Saved requests only')
            assert (state / 'fixture-worker-starts.txt').read_bytes() == starts
            checks += 1
            session.send(b'v')
            session.until(b'Current request: en')
            session.send(b'\x1b')
            session.until(b'> Language ID:')
            session.send(b'\x1b')
            session.until(b'CanaryMiner / Settings')
            session.send(b'\x1b')
            session.until(b'[S]')
            session.send(b'q')
            session.finished()
            assert (state / 'fixture-worker-starts.txt').read_bytes() == starts
            checks += 2
    print(f'language PTY: {checks} review/save/discard/conflict/resize/SIGINT/frozen-request/unchanged-worker checks')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    main()
