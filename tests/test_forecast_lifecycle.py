"""Kill only an owned synthetic parent and verify its parent-bound read helper terminates; no production process access."""
import ctypes
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def wait(predicate, seconds=8):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if predicate():
            return True
        time.sleep(0.01)
    return bool(predicate())


def process_identity(pid):
    if os.name == 'nt':
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
        kernel.OpenProcess.restype = ctypes.c_void_p
        kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        kernel.WaitForSingleObject.restype = ctypes.c_uint32
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = kernel.OpenProcess(0x100000, False, pid)
        assert handle, 'cannot observe the owned fixture child'
        return lambda: kernel.WaitForSingleObject(handle, 0) == 258, lambda: kernel.CloseHandle(handle)

    def current():
        try:
            fields = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
            return fields[19], fields[0]
        except (FileNotFoundError, ProcessLookupError):
            # procfs can report ESRCH when the task exits after open but before read.
            return None

    original = current()
    assert original and original[1] != 'Z'
    return lambda: (value := current()) is not None and value[0] == original[0] and value[1] != 'Z', lambda: None


def main():
    fixture, writer, reader, stalled, library = [Path(p).resolve() for p in sys.argv[1:6]]
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-forecast-life-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        parent = subprocess.Popen([str(fixture), str(writer), str(stalled), str(root), 'parent-probe'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        close = lambda: None
        try:
            marker = root / 'fixture-query-hang.db.fixture-ready'
            assert wait(lambda: marker.exists() and marker.read_text().strip().isdigit())
            alive, close = process_identity(int(marker.read_text()))
            assert alive() and parent.poll() is None
            files = [root / 'fixture-query-hang.db', root / 'fixture-query-hang.db.credit-recovery.json']
            before = [hashlib.sha256(p.read_bytes()).digest() for p in files]
            parent.kill()
            parent.wait(timeout=5)
            assert wait(lambda: not alive())
            assert [hashlib.sha256(p.read_bytes()).digest() for p in files] == before
            checks += 4
            fresh = subprocess.run([str(fixture), str(writer), str(reader), str(root), 'read-probe'], capture_output=True, timeout=10)
            assert fresh.returncode == 0 and b'synthetic committed read ready' in fresh.stdout
            assert [hashlib.sha256(p.read_bytes()).digest() for p in files] == before
            checks += 2
        finally:
            if parent.poll() is None:
                parent.kill()
                parent.wait(timeout=5)
            close()
            if parent.stdout:
                parent.stdout.close()
            if parent.stderr:
                parent.stderr.close()
    for path in (reader, library):
        data = path.read_bytes()
        for marker in (b'fixture-query-hang.db', b'fixture-query-between.db'):
            assert marker not in data, 'test stall leaked into ordinary artifact'
            checks += 1
    print(f'forecast lifecycle: {checks} checks passed; owned synthetic parent/child only')


if __name__ == '__main__':
    main()
