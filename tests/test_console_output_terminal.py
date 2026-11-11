"""Actual inert idle entry under owned terminal backpressure. No device, source, or user terminal."""
import json
import os
import platform
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
    if sys.platform != 'linux' or platform.machine() not in ('x86_64', 'AMD64'):
        raise SystemExit('Linux x86-64 owned-procfs/PTY qualification only')
    import fcntl
    import pty
    import termios
    from test_forecast_lifecycle import process_identity, wait
    cli, helper = (Path(value).resolve() for value in sys.argv[1:3])
    failures = []
    for blocked in (False, True):
        for stop in ('q', 'signal'):
            name = ('blocked-' if blocked else 'healthy-') + stop
            with tempfile.TemporaryDirectory(prefix='canary-console-pressure-') as directory:
                root = Path(directory)
                binary = root / 'pressure-cli'
                sibling = root / 'canary_preferences_worker'
                shutil.copy2(cli, binary)
                shutil.copy2(helper, sibling)
                state = root / 'state'
                state.mkdir()
                config = root / 'config.json'
                original = json.dumps({'schema_version': 1, 'state_dir': str(state)}).encode()
                config.write_bytes(original)
                master, slave = pty.openpty()
                fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 100, 0, 0))
                child = subprocess.Popen([str(binary), 'settings', '--config', str(config)], stdin=slave,
                                         stdout=slave, stderr=slave, start_new_session=True)
                os.close(slave)
                transcript = bytearray()
                identities = {}

                def drain():
                    if select.select([master], [], [], .02)[0]:
                        try:
                            transcript.extend(os.read(master, 65536))
                        except OSError:
                            pass

                def writers():
                    owned = [child.pid]
                    try:
                        children = set()
                        for task in (Path('/proc') / str(child.pid) / 'task').iterdir():
                            try:
                                children.update((task / 'children').read_text().split())
                            except (OSError, ProcessLookupError):
                                pass
                        for value in children:
                            process = Path('/proc') / value
                            args = (process / 'cmdline').read_bytes().split(b'\0')
                            if args and args[0] == os.fsencode(sibling) and any(flag in args for flag in (b'--local-stream-v1', b'--local-output-v1')):
                                pid = int(value)
                                if pid not in identities:
                                    identities[pid] = process_identity(pid)
                                if b'--local-output-v1' in args:
                                    owned.append(pid)
                    except (OSError, ProcessLookupError):
                        pass
                    return owned

                try:
                    deadline = time.monotonic() + 10
                    while b'CanaryMiner / Settings' not in transcript:
                        assert child.poll() is None and time.monotonic() < deadline, name + ': settings readiness'
                        drain()
                    writers()
                    if blocked:
                        # No reader is running. Require a real sustained fd1 write, not just a sleep or full-queue guess.
                        deadline = time.monotonic() + 3
                        samples = 0
                        while samples < 3 and time.monotonic() < deadline:
                            assert child.poll() is None, name + ': premature exit before stop'
                            writing = False
                            for pid in writers():
                                try:
                                    fields = (Path('/proc') / str(pid) / 'syscall').read_text().split()
                                    writing = writing or (len(fields) > 1 and fields[0] == '1' and int(fields[1], 0) == 1)
                                except OSError:
                                    pass
                            samples = samples + 1 if writing else 0
                            time.sleep(.05)
                        assert samples == 3, name + ': actual output backpressure was not established'
                    if stop == 'q':
                        os.write(master, b'q')
                    else:
                        child.send_signal(signal.SIGINT)
                    deadline = time.monotonic() + 1
                    while child.poll() is None and time.monotonic() < deadline:
                        if blocked:
                            time.sleep(.01)
                        else:
                            drain()
                    stopped = child.poll() is not None
                    if not stopped:
                        failures.append(name)
                        deadline = time.monotonic() + 5
                        while child.poll() is None and time.monotonic() < deadline:
                            drain()
                        child.wait(timeout=2)
                    assert child.returncode == 0, name + ': expected clean intentional stop'
                    assert config.read_bytes() == original and not tuple(state.iterdir()), name + ': originals changed'
                    assert b'FORBIDDEN_DEVICE_SETUP' not in transcript, name + ': device admission'
                    assert all(wait(lambda alive=identity[0]: not alive()) for identity in identities.values()), name + ': owned helper survived'
                    print(name + ': ' + ('PASS' if stopped else 'FAIL - owned output release was required'), flush=True)
                finally:
                    if child.poll() is None:
                        child.kill()
                        child.wait(timeout=3)
                    os.close(master)
                    for _, close in identities.values():
                        close()
    assert not failures, 'Idle stop was blocked by console output: ' + ', '.join(failures)
    print('console_output_terminal: four actual idle healthy/backpressure Q/SIGINT cases; originals preserved, owned helpers retired')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    main()
