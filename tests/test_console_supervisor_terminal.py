"""Actual supervisor stop under retained stdout, with only owned CPU workers and terminals."""
import os
from pathlib import Path
import platform
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
        raise SystemExit('Linux x86-64 owned-procfs qualification only')
    import fcntl
    import pty
    import termios
    from test_forecast_lifecycle import process_identity, wait
    fixture, helper = [Path(value).resolve() for value in sys.argv[1:3]]
    failures = []
    for mode, stop in (('dashboard', 'q'), ('dashboard', 'signal'), ('plain', 'signal')):
        for blocked in (False, True):
            name = mode + '-' + ('blocked-' if blocked else 'healthy-') + stop
            with tempfile.TemporaryDirectory(prefix='canary-console-supervisor-') as directory:
                root = Path(directory)
                binary = root / 'supervisor-fixture'
                sibling = root / 'canary_preferences_worker'
                shutil.copy2(fixture, binary)
                shutil.copy2(helper, sibling)
                state = root / 'state'
                state.mkdir()
                master = None
                if mode == 'dashboard':
                    master, slave = pty.openpty()
                    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 100, 0, 0))
                    child = subprocess.Popen([str(binary), mode, str(state)], stdin=slave, stdout=slave, stderr=slave, start_new_session=True)
                    os.close(slave)
                    descriptor = master
                else:
                    child = subprocess.Popen([str(binary), mode, str(state)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    descriptor = child.stdout.fileno()
                    fcntl.fcntl(descriptor, fcntl.F_SETPIPE_SZ, 4096)
                transcript = bytearray()
                identities = {}

                def drain():
                    if select.select([descriptor], [], [], .01)[0]:
                        try:
                            transcript.extend(os.read(descriptor, 65536))
                        except OSError:
                            pass

                def owned():
                    values = set()
                    try:
                        for task in (Path('/proc') / str(child.pid) / 'task').iterdir():
                            try:
                                values.update((task / 'children').read_text().split())
                            except OSError:
                                pass
                        result = []
                        for value in values:
                            args = (Path('/proc') / value / 'cmdline').read_bytes().split(b'\0')
                            if args[0] in (os.fsencode(binary), os.fsencode(sibling)):
                                pid = int(value)
                                if pid not in identities:
                                    identities[pid] = process_identity(pid)
                                result.append(pid)
                        return result
                    except OSError:
                        return []

                try:
                    deadline = time.monotonic() + 5
                    while not (state / 'worker-starts').exists() or (mode == 'dashboard' and b'[S]' not in transcript):
                        assert child.poll() is None and time.monotonic() < deadline, name + ': startup'
                        if mode == 'dashboard' or not blocked:
                            drain()
                    if mode == 'dashboard':
                        os.write(master, b's')
                        while b'CanaryMiner / Settings' not in transcript:
                            assert child.poll() is None and time.monotonic() < deadline, name + ': settings readiness'
                            drain()
                    owned()
                    if blocked:
                        samples = 0
                        deadline = time.monotonic() + 3
                        while samples < 3 and time.monotonic() < deadline:
                            assert child.poll() is None, name + ': premature exit'
                            writing = False
                            for pid in [child.pid, *owned()]:
                                try:
                                    fields = (Path('/proc') / str(pid) / 'syscall').read_text().split()
                                    writing |= len(fields) > 1 and fields[0] == '1' and int(fields[1], 0) == 1
                                except OSError:
                                    pass
                            samples = samples + 1 if writing else 0
                            time.sleep(.05)
                        assert samples == 3, name + ': sustained actual fd1 write not established'
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
                    assert child.returncode == 0, name + ': clean intentional exit'
                    while select.select([descriptor], [], [], 0)[0]:
                        before = len(transcript)
                        drain()
                        if len(transcript) == before:
                            break
                    assert (state / 'worker-starts').read_bytes() == b'start\n', name + ': worker relaunched'
                    assert {path.name for path in state.iterdir()} == {'worker-starts'}, name + ': unexpected persistence'
                    assert b'synthetic private value' not in transcript, name + ': redaction changed'
                    assert all(wait(lambda alive=identity[0]: not alive()) for identity in identities.values()), name + ': owned child survived'
                    if not blocked:
                        assert b'stopped: 1 GPU(s)' in transcript, name + ': healthy final summary missing'
                    print(name + ': ' + ('PASS' if stopped else 'FAIL - stdout release required'), flush=True)
                finally:
                    if child.poll() is None:
                        child.kill()
                        child.wait(timeout=3)
                    if master is not None:
                        os.close(master)
                    else:
                        for stream in (child.stdin, child.stdout, child.stderr):
                            stream.close()
                    for _, close in identities.values():
                        close()
    assert not failures, 'Actual supervisor stop blocked by stdout: ' + ', '.join(failures)
    print('console supervisor: six dashboard/plain Q/SIGINT cases; no devices/endpoints, owned children retired')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    main()
