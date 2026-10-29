"""Owned synthetic parents: death releases helpers/leases; successor fencing and artifact boundaries."""
from contextlib import closing
import ctypes
import hashlib
import json
import os
from pathlib import Path
import queue
import signal
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time


def main():
    probe, authorization, collector, fixture, library = (Path(p).resolve(strict=True) for p in sys.argv[1:6])
    checks = 0

    def check(value):
        nonlocal checks
        assert value
        checks += 1

    def wait(predicate):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            if predicate():
                return True
            time.sleep(.01)
        return predicate()

    def args(file, helper, action):
        return [str(probe), '--probe', str(authorization), str(helper), str(file), action]

    def once(file, helper=collector):
        result = subprocess.run(args(file, helper, 'once'), capture_output=True, encoding='utf8', timeout=12)
        check(result.returncode == 0 and result.stderr == '')
        return result.stdout.strip()

    def seal(record):
        body = json.dumps(record[:-1], separators=(',', ':'), ensure_ascii=False)[:-1]
        record[-1] = hashlib.sha256(body.encode()).hexdigest()
        return json.dumps(record, separators=(',', ':'), ensure_ascii=False).encode()

    def facts(file):
        document = json.loads(file.read_bytes())
        database = file.parent / ('energy-history.' + document[6] + '.db')
        with closing(sqlite3.connect(database)) as db:
            return db.execute('SELECT sequence,digest FROM meta').fetchone(), db.execute('SELECT body FROM events ORDER BY sequence').fetchall()

    class Child:
        """Retain exact OS identity for a test-created helper; never signal unrelated processes."""
        def __init__(self, pid):
            self.pid = pid
            self.handle = None
            if os.name == 'nt':
                self.api = ctypes.WinDLL('kernel32', use_last_error=True)
                self.api.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
                self.api.OpenProcess.restype = ctypes.c_void_p
                self.api.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
                self.api.CloseHandle.argtypes = [ctypes.c_void_p]
                self.api.TerminateProcess.argtypes = [ctypes.c_void_p, ctypes.c_uint]
                self.handle = self.api.OpenProcess(0x100000 | 0x1000 | 1, False, pid)
                check(bool(self.handle))
            else:
                self.identity = self.stat()[1]

        def stat(self):
            try:
                fields = Path('/proc', str(self.pid), 'stat').read_text().split(') ', 1)[1].split()
                return fields[0], fields[19]
            except FileNotFoundError:
                return '', ''

        def exited(self):
            if self.handle:
                return self.api.WaitForSingleObject(self.handle, 0) == 0
            state, identity = self.stat()
            return state in ('', 'Z') or identity != self.identity

        def close(self):
            # Failure cleanup is confined to the identities the fixture supplied before parent termination.
            if not self.exited():
                if self.handle:
                    self.api.TerminateProcess(self.handle, 99)
                else:
                    os.kill(self.pid, signal.SIGKILL)
            if self.handle:
                self.api.CloseHandle(self.handle)

    with tempfile.TemporaryDirectory(prefix='canary-collector-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        for action in ('hold', 'guard'):
            folder = root / action
            folder.mkdir()
            file = folder / 'fixture-parent-death.json'
            children = []
            parent = subprocess.Popen(args(file, fixture, action), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding='utf8')
            lines = queue.Queue()
            reader = threading.Thread(target=lambda: lines.put(parent.stdout.readline()), daemon=True)
            reader.start()
            try:
                check(lines.get(timeout=10).strip() == 'held')
                marker = Path(str(file) + '.fixture-pid')
                check(wait(lambda: marker.exists() and marker.stat().st_size > 0))
                children.append(Child(int(marker.read_text())))
                if action == 'guard':
                    marker = Path(str(file) + '.fixture-guard-pid')
                    check(wait(lambda: marker.exists() and marker.stat().st_size > 0))
                    children.append(Child(int(marker.read_text())))
                before = file.read_bytes()
                fence_path = Path(str(file) + '.collector.json')
                old_fence = fence_path.read_bytes()
                old = json.loads(before)
                old_record = json.loads(old_fence)
                check(seal(old_record) == old_fence and old_record[2] == old)
                check(b'GPU-' not in old_fence and b'fixture' not in old_fence)
                initial_facts = facts(file)
                check(initial_facts[0][0] == 0 and initial_facts[1] == [])
                # A separately spawned parent is refused without consuming a session or a fence.
                check(once(file) == 'denied' and file.read_bytes() == before and fence_path.read_bytes() == old_fence)
                start = time.monotonic()
                parent.kill()
                parent.wait(timeout=3)
                check(wait(lambda: all(c.exited() for c in children)))
                check(time.monotonic() - start < 8)
                check(file.read_bytes() == before and fence_path.read_bytes() == old_fence and facts(file) == initial_facts)
                check(once(file) == 'held')
                current = json.loads(file.read_bytes())
                fresh = json.loads(fence_path.read_bytes())
                check(current[8] != old[8] and fresh[1] != old_record[1])
                check(current[1] == old[1] + 1 and current[6:8] == old[6:8] and facts(file) == initial_facts)
                if os.name != 'nt':
                    check(fence_path.stat().st_mode & 0o777 == 0o600)
            finally:
                if parent.poll() is None:
                    parent.kill()
                parent.wait(timeout=3)
                reader.join(timeout=1)
                for child in children:
                    child.close()
                parent.stdout.close()
                parent.stderr.close()

        # Valid checksums do not authorize changing an old reservation's immutable budgets or chronology.
        file = root / 'record.json'
        check(once(file) == 'held')
        fence_path = Path(str(file) + '.collector.json')
        good_fence, good_doc, good_facts = fence_path.read_bytes(), file.read_bytes(), facts(file)
        for kind in ('limits', 'revision', 'clock', 'checksum', 'oversize'):
            record = json.loads(good_fence)
            if kind == 'limits':
                record[2][4][2] -= 1
            if kind == 'revision':
                record[2][1] += 1
            if kind == 'clock':
                record[2][2] += 1
            record[2] = json.loads(seal(record[2]))
            damaged = seal(record)
            if kind == 'checksum':
                damaged = damaged[:-2] + b'x]'
            if kind == 'oversize':
                damaged = b' ' * (24 * 1024 + 1)
            fence_path.write_bytes(damaged)
            check(once(file) == 'denied')
            check(file.read_bytes() == good_doc and fence_path.read_bytes() == damaged and facts(file) == good_facts)
        fence_path.write_bytes(good_fence)
        check(once(file) == 'held')

    for path in (collector, library):
        blob = path.read_bytes()
        for marker in (b'fixture-lease-no-ack', b'fixture-lease-extra', b'fixture-parent-death', b'fixture-guard-delay', b'fixture-guard-in-operation', b'.fixture-guard-pid'):
            check(marker not in blob and marker.decode().encode('utf-16le') not in blob)
    print(f'energy collector lifecycle: {checks} checks, 0 failures')


if __name__ == '__main__':
    main()
