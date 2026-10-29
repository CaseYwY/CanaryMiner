"""Owned synthetic parent death only; readonly helper and source originals must remain bounded/preserved."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from test_forecast_lifecycle import process_identity, wait


def main():
    fixture, writer, reader, stalled, library = [Path(p).resolve(strict=True) for p in sys.argv[1:6]]
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-energy-trace-life-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        parent = subprocess.Popen([str(fixture), str(writer), str(reader), str(stalled), 'parent-probe', str(root)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        close = lambda: None
        try:
            file = root / 'fixture.energy-trace-hang.db'
            marker = Path(str(file) + '.fixture-ready')
            assert wait(lambda: marker.exists() and marker.read_text().strip().isdigit())
            alive, close = process_identity(int(marker.read_text()))
            assert alive() and parent.poll() is None
            originals = [file, Path(str(file) + '.energy-recovery.json')]
            before = [hashlib.sha256(p.read_bytes()).digest() for p in originals]
            parent.kill()
            parent.wait(timeout=5)
            assert wait(lambda: not alive())
            assert [hashlib.sha256(p.read_bytes()).digest() for p in originals] == before
            checks += 4
            fresh = subprocess.run([str(fixture), str(writer), str(reader), str(stalled), 'read-probe', str(root)], capture_output=True, timeout=10)
            assert fresh.returncode == 0 and b'synthetic committed trace ready' in fresh.stdout
            assert [hashlib.sha256(p.read_bytes()).digest() for p in originals] == before
            checks += 2
        finally:
            if parent.poll() is None:
                parent.kill()
                parent.wait(timeout=5)
            close()
            parent.stdout.close()
            parent.stderr.close()
    for path in (reader, library):
        for marker in (b'fixture.energy-trace-hang.db', b'fixture.energy-trace-extra.db'):
            assert marker not in path.read_bytes()
            checks += 1
    print(f'energy trace lifecycle: {checks} checks passed; parent-bound reads and preserved source originals')


if __name__ == '__main__':
    main()
