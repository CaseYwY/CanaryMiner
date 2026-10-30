"""Owned CPU worker lifecycle and opt-in CLI admission; no device, game or external endpoint."""
import ctypes
import os
from pathlib import Path
import queue
import select
import signal
import subprocess
import sys
import threading


def parent_death(executable):
    process = subprocess.Popen([str(executable), '--parent-death'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    lines = queue.Queue(maxsize=1)
    reader = threading.Thread(target=lambda: lines.put(process.stdout.readline(128)), daemon=True)
    reader.start()
    handle = None
    kernel = None
    try:
        fields = lines.get(timeout=6).split()
        assert len(fields) == 2 and fields[0] == b'CPUPID', fields
        pid = int(fields[1])
        if os.name == 'nt':
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
            kernel.OpenProcess.restype = ctypes.c_void_p
            kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
            kernel.WaitForSingleObject.restype = ctypes.c_ulong
            kernel.TerminateProcess.argtypes = [ctypes.c_void_p, ctypes.c_uint]
            kernel.CloseHandle.argtypes = [ctypes.c_void_p]
            handle = kernel.OpenProcess(0x100000 | 0x0001, False, pid)
            assert handle
        else:
            handle = os.pidfd_open(pid)
        process.kill()  # only the test parent we just created; the owned worker must die through its parent binding
        process.wait(timeout=5)
        if kernel:
            assert kernel.WaitForSingleObject(handle, 2000) == 0
        else:
            poller = select.poll()
            poller.register(handle, select.POLLIN)
            assert poller.poll(2000)
        reader.join(timeout=1)
        assert not reader.is_alive()
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)
        if handle is not None:
            if kernel:
                if kernel.WaitForSingleObject(handle, 0) != 0:
                    kernel.TerminateProcess(handle, 8)
                kernel.CloseHandle(handle)
            else:
                poller = select.poll()
                poller.register(handle, select.POLLIN)
                if not poller.poll(0):
                    signal.pidfd_send_signal(handle, signal.SIGKILL)
                os.close(handle)
        process.stdout.close()
        process.stderr.close()


def cli_admission(ordinary, enabled):
    checks = 0
    handles = ['3', '4', '5'] if os.name == 'nt' else ['3', '3', '1']
    flag = ['--worker-control-v1', *handles]
    environment = {k: v for k, v in os.environ.items() if not k.startswith('CANARY_PEARL_')}
    for executable, args in (
        (ordinary, flag),
        (enabled, ['--worker-control-v1']),
        (enabled, ['--worker-control-v1', '03', '3', '1']),
        (enabled, flag),
        (enabled, [*flag, *flag]),
        (enabled, ['list-devices', *flag]),
        (enabled, ['--worker-child', '--worker-id', '0', '--bench', *flag]),
        (enabled, ['--worker-child', '--worker-id', '0', '--autotune', *flag]),
    ):
        result = subprocess.run([str(executable), *args], env=environment, capture_output=True, timeout=8)
        assert result.returncode == 2, (result.returncode, result.stderr)
        assert b'FORBIDDEN_DEVICE_SETUP' not in result.stderr
        checks += 1
    args = ['run', '--algo', 'pearl-pow', '--worker-child', '--worker-id', '0', '--pool', 'fixture.invalid:1', '--wallet', 'prl1' + 'q' * 59,
            '--pearl-network', 'mainnet', '--pearl-target-semantics', 'base-before-penalty', *flag]
    result = subprocess.run([str(enabled), *args], env=environment, capture_output=True, timeout=8)
    assert result.returncode == 8 and b'parent/channel binding failed' in result.stderr, result.stderr
    assert b'FORBIDDEN_DEVICE_SETUP' not in result.stderr
    return checks + 1


def main():
    executable, ordinary, enabled = [Path(arg).resolve() for arg in sys.argv[1:]]
    parent_death(executable)
    if os.name != 'nt':
        result = subprocess.run([str(executable), '--stolen-reap'], capture_output=True, timeout=8)
        assert result.returncode == 0, result.stderr
    checks = cli_admission(ordinary, enabled)
    print(f'worker process lifecycle: parent death before lease expiry; {checks} native CLI refusals; no device activation')


if __name__ == '__main__':
    main()
