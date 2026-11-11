"""Actual inert CLI and genuine sibling blocked on an owned FIFO; never a device backend or real configuration."""
import errno
import json
import os
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
    if os.name != 'posix':
        raise SystemExit('POSIX FIFO/PTY only; CPU-only CLI stub required, never a miner binary')
    import fcntl
    import pty
    import termios
    cli, helper = [Path(value).resolve() for value in sys.argv[1:3]]
    checks = 0

    def held(use_signal=False, release=None, settings=False, key=b'q'):
        nonlocal checks
        with tempfile.TemporaryDirectory(prefix='canary-startup-config-') as directory:
            root = Path(directory)
            binary = root/'startup-cli'
            shutil.copy2(cli, binary)
            shutil.copy2(helper, root/'canary_preferences_worker')
            state = root/'state'
            state.mkdir()
            original = b'fixture.private-original-not-for-output'
            (state/'unrelated.txt').write_bytes(original)
            config = root/'held-config'
            os.mkfifo(config)
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 100, 0, 0))

            def own_terminal():
                os.setsid()
                fcntl.ioctl(0, termios.TIOCSCTTY, 0)

            child = subprocess.Popen([str(binary), *(['settings'] if settings else []), '--config', str(config)], cwd=root,
                                     stdin=slave, stdout=slave, stderr=slave, preexec_fn=own_terminal)
            os.close(slave)
            transcript = b''
            writer = None

            def observe():
                nonlocal transcript
                if select.select([master], [], [], .02)[0]:
                    try:
                        transcript += os.read(master, 65536)
                    except OSError:
                        pass

            try:
                deadline = time.monotonic()+8
                while b'Startup configuration PENDING' not in transcript or writer is None:
                    assert child.poll() is None, 'startup bypassed the held original before inspection'
                    assert time.monotonic() < deadline, 'main config still blocks the foreground before cancellable inspection'
                    observe()
                    if b'Startup configuration PENDING' in transcript and writer is None:
                        try:
                            writer = os.open(config, os.O_WRONLY | os.O_NONBLOCK)
                        except OSError as error:
                            if error.errno != errno.ENXIO:
                                raise
                # A writer can open only after the genuine helper has opened the FIFO; zero bytes keep its read held.
                assert b'FORBIDDEN_DEVICE_SETUP' not in transcript and list(state.iterdir()) == [state/'unrelated.txt']
                if release is None:
                    began = time.monotonic()
                    if use_signal:
                        child.send_signal(signal.SIGINT)
                    else:
                        os.write(master, key)
                    child.wait(timeout=4)
                    assert child.returncode == 0 and time.monotonic()-began < 3
                else:
                    data = json.dumps({'schema_version': 1, 'state_dir': str(state)}).encode() if release == 'valid' else b'{broken-private-original'
                    assert os.write(writer, data) == len(data)
                    os.close(writer)
                    writer = None
                    deadline = time.monotonic()+12
                    while child.poll() is None:
                        assert time.monotonic() < deadline, 'released original did not settle'
                        observe()
                    observe()
                    if release == 'valid':
                        assert child.returncode == 97 and b'FORBIDDEN_DEVICE_SETUP' in transcript
                    else:
                        assert child.returncode == 3 and b'config' in transcript
                        assert b'FORBIDDEN_DEVICE_SETUP' not in transcript and b'broken-private-original' not in transcript
                assert (state/'unrelated.txt').read_bytes() == original
                assert list(state.iterdir()) == [state/'unrelated.txt'] and config.is_fifo()
                assert b'fixture.private-original-not-for-output' not in transcript
                checks += 1
            finally:
                if child.poll() is None:
                    child.send_signal(signal.SIGTERM)
                    try:
                        child.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        child.kill()
                        child.wait(timeout=3)
                if writer is not None:
                    os.close(writer)
                os.close(master)

    held()
    held(use_signal=True)
    held(key=b'\x1b')
    held(settings=True)
    held(use_signal=True, settings=True)
    held(release='valid')
    held(release='malformed')
    with tempfile.TemporaryDirectory(prefix='canary-startup-config-') as directory:
        root = Path(directory)
        binary = root/'startup-cli'
        shutil.copy2(cli, binary)
        shutil.copy2(helper, root/'canary_preferences_worker')
        config = root/'unread-config'
        os.mkfifo(config)
        result = subprocess.run([str(binary), 'settings', '--config', str(config)], cwd=root, capture_output=True, timeout=4)
        assert result.returncode == 2 and b'requires an interactive terminal' in result.stderr
        assert b'FORBIDDEN_DEVICE_SETUP' not in result.stdout+result.stderr
        checks += 1
        result = subprocess.run([str(binary), 'version', '--config', str(config)], cwd=root, capture_output=True, timeout=4)
        assert result.returncode == 0 and b'CanaryMiner' in result.stdout
        checks += 1
        config.unlink()
        prefix = json.dumps({'schema_version': 1, 'state_dir': str(root/'state')}).encode()
        for size, expected in ((70000, 97), (1 << 20, 97), ((1 << 20)+1, 3)):
            document = prefix+b' '*(size-len(prefix))
            config.write_bytes(document)
            result = subprocess.run([str(binary), '--config', str(config)], cwd=root, capture_output=True, timeout=20)
            assert result.returncode == expected, result.stderr
            assert (b'FORBIDDEN_DEVICE_SETUP' in result.stderr) == (expected == 97)
            assert config.read_bytes() == document and not (root/'state').exists()
            checks += 1
        config.write_bytes(prefix)
        (root/'canary_preferences_worker').unlink()
        result = subprocess.run([str(binary), '--config', str(config)], cwd=root, capture_output=True, timeout=4)
        assert result.returncode == 3 and b'Startup configuration unavailable' in result.stderr
        assert b'FORBIDDEN_DEVICE_SETUP' not in result.stdout+result.stderr and config.read_bytes() == prefix
        assert not (root/'state').exists()
        checks += 1
    print('startup_config_terminal:', checks, 'genuine held-FIFO cancellation/release/refusal/bypass and exact-size cases; no device backend or configuration writes')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    main()
