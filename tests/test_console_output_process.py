"""Actual native pipe backpressure/parent death, byte oracle and owned-helper containment."""
import os
from pathlib import Path
import platform
import subprocess
import sys
import threading
import time


def children(parent, helper):
    if os.name == 'nt':
        import ctypes
        from ctypes import wintypes
        class Entry(ctypes.Structure):
            _fields_ = [('dwSize', wintypes.DWORD), ('cntUsage', wintypes.DWORD), ('pid', wintypes.DWORD),
                        ('heap', ctypes.c_size_t), ('module', wintypes.DWORD), ('threads', wintypes.DWORD),
                        ('parent', wintypes.DWORD), ('priority', wintypes.LONG), ('flags', wintypes.DWORD),
                        ('exe', wintypes.WCHAR * 260)]
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
        kernel.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
        kernel.Process32FirstW.argtypes = kernel.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(Entry)]
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        snapshot = kernel.CreateToolhelp32Snapshot(2, 0)
        assert snapshot not in (None, ctypes.c_void_p(-1).value)
        result = []
        try:
            entry = Entry()
            entry.dwSize = ctypes.sizeof(entry)
            more = kernel.Process32FirstW(snapshot, ctypes.byref(entry))
            while more:
                # Only the oracle-owned parent's children are opened; no user process is probed.
                if entry.parent == parent and entry.exe.casefold() == helper.name.casefold():
                    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
                    kernel.OpenProcess.restype = wintypes.HANDLE
                    kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
                    handle = kernel.OpenProcess(0x1000, False, entry.pid)
                    if handle:
                        try:
                            name, length = ctypes.create_unicode_buffer(32768), wintypes.DWORD(32768)
                            if kernel.QueryFullProcessImageNameW(handle, 0, name, ctypes.byref(length)) and os.path.normcase(name.value) == os.path.normcase(str(helper)):
                                result.append(entry.pid)
                        finally:
                            kernel.CloseHandle(handle)
                more = kernel.Process32NextW(snapshot, ctypes.byref(entry))
            return result
        finally:
            kernel.CloseHandle(snapshot)
    result = set()
    try:
        for task in (Path('/proc') / str(parent) / 'task').iterdir():
            try:
                result.update((task / 'children').read_text().split())
            except OSError:
                pass
        return [int(pid) for pid in result if (Path('/proc') / pid / 'cmdline').read_bytes().split(b'\0')[0] == os.fsencode(helper)]
    except OSError:
        return []


def windows_pipe_full(stream):
    import ctypes
    import msvcrt
    from ctypes import wintypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.GetNamedPipeInfo.argtypes = [wintypes.HANDLE, *([ctypes.POINTER(wintypes.DWORD)] * 4)]
    kernel.PeekNamedPipe.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD, *([ctypes.POINTER(wintypes.DWORD)] * 3)]
    handle = msvcrt.get_osfhandle(stream.fileno())
    output, input_size, available = wintypes.DWORD(), wintypes.DWORD(), wintypes.DWORD()
    assert kernel.GetNamedPipeInfo(handle, None, ctypes.byref(output), ctypes.byref(input_size), None)
    assert kernel.PeekNamedPipe(handle, None, 0, None, ctypes.byref(available), None)
    capacity = max(output.value, input_size.value)
    return 0 < capacity < 262144 and available.value >= capacity


def main():
    if os.name != 'nt' and (sys.platform != 'linux' or platform.machine() not in ('x86_64', 'AMD64')):
        raise SystemExit('Windows or Linux x86-64 owned-process qualification only')
    from test_forecast_lifecycle import process_identity, wait
    fixture, helper = [Path(value).resolve() for value in sys.argv[1:3]]
    checks = 0
    for mode in ('healthy', 'stopped-before-start', 'invalid', 'stop', 'coalesce', 'overflow', 'foreign', 'epoch', 'timeout', 'parent-death'):
        actual_mode = 'stop' if mode == 'parent-death' else mode
        child = subprocess.Popen([str(fixture), str(helper), actual_mode], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0,
                                 creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        identities = []
        stderr = bytearray()
        reader = None
        try:
            if mode in ('healthy', 'stopped-before-start', 'invalid'):
                output, error = child.communicate(timeout=10)
                assert child.returncode == 0, error.decode(errors='replace')
                expected = bytes(range(256)) * 1024 + b'\nORDERED-TAIL\n' if mode == 'healthy' else b''
                assert output == expected, mode + ': exact binary bytes/order'
                checks += 2
                continue
            def capture_error():
                while data := child.stderr.read(4096):
                    stderr.extend(data)
            reader = threading.Thread(target=capture_error)
            reader.start()
            deadline = time.monotonic() + 5
            blocked = 0
            while blocked < 3:
                assert child.poll() is None and time.monotonic() < deadline, mode + ': actual fd1-write barrier'
                pids = children(child.pid, helper)
                writing = bool(pids) and windows_pipe_full(child.stdout) if os.name == 'nt' else False
                if os.name != 'nt':
                    for pid in pids:
                        try:
                            fields = (Path('/proc') / str(pid) / 'syscall').read_text().split()
                            writing |= len(fields) > 1 and fields[0] == '1' and int(fields[1], 0) == 1
                        except OSError:
                            pass
                blocked = blocked + 1 if writing and b'ISSUED\n' in stderr.replace(b'\r', b'') else 0
                time.sleep(.02)
            assert len(pids) == (2 if os.name == 'nt' else 1), 'original and Windows pre-created finalizer ownership'
            identities = [process_identity(pid) for pid in pids]
            # No private files or inherited capabilities beyond the single IPC socket/stdout role.
            if os.name != 'nt':
                process = Path('/proc') / str(pids[0])
                descriptors = {int(path.name) for path in (process / 'fd').iterdir()}
                assert descriptors == {0, 1, 2, 3}, descriptors
                assert (process / 'environ').read_bytes().split(b'\0') == [b'LANG=C', b'LC_ALL=C', b'']
            if mode == 'parent-death':
                child.kill()
                child.wait(timeout=3)
            else:
                if mode != 'timeout':
                    child.stdin.write(b'blocked\n')
                    child.stdin.flush()
                # Crucially do not drain stdout to make Stop or timeout pass.
                child.wait(timeout=3)
                reader.join(timeout=3)
                assert not reader.is_alive()
                assert child.returncode == 0, (mode, stderr.decode(errors='replace'))
            assert all(wait(lambda alive=identity[0]: not alive()) for identity in identities), mode + ': original helper retired'
            checks += 4 if os.name == 'nt' else 6
            print(mode + ': PASS (owned output retained under native backpressure; original helper retired)', flush=True)
        finally:
            if child.poll() is None:
                child.kill()
                child.wait(timeout=3)
            if reader is not None:
                reader.join(timeout=3)
                assert not reader.is_alive(), 'owned stderr reader did not finish'
            for stream in (child.stdin, child.stdout, child.stderr):
                if stream:
                    stream.close()
            for _, close in identities:
                close()
    print(f'console_output_process: {checks} checks across ten owned native scenarios, no devices/network/user terminal')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    main()
