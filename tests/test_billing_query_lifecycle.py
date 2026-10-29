"""Synthetic parent-bound reader lifetime and readonly reconstruction of pending/committed monetary originals."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from test_forecast_lifecycle import process_identity, wait


def main():
    probe, reader, stalled, wrong, pending, library = [Path(p).resolve(strict=True) for p in sys.argv[1:7]]
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-billing-query-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        command = [str(probe), str(reader), str(stalled), str(wrong)]
        parent = subprocess.Popen(command + ['parent-probe', str(root)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        close = lambda: None
        try:
            file = root / 'fixture.billing-query-hang.db'
            marker = Path(str(file) + '.fixture-ready')
            assert wait(lambda: marker.exists() and marker.read_text().strip().isdigit())
            alive, close = process_identity(int(marker.read_text()))
            assert alive() and parent.poll() is None
            originals = [file, Path(str(file) + '.billing-recovery')]
            before = [hashlib.sha256(p.read_bytes()).digest() for p in originals]
            parent.kill()
            parent.wait(timeout=5)
            assert wait(lambda: not alive())
            assert [hashlib.sha256(p.read_bytes()).digest() for p in originals] == before
            checks += 4
            fresh = subprocess.run(command + ['read-probe', str(root)], capture_output=True, timeout=10)
            assert fresh.returncode == 0 and b'synthetic committed monetary view ready' in fresh.stdout
            assert [hashlib.sha256(p.read_bytes()).digest() for p in originals] == before
            checks += 2
        finally:
            if parent.poll() is None:
                parent.kill()
                parent.wait(timeout=5)
            close()
            parent.stdout.close()
            parent.stderr.close()
        for action, exit_code, sequence in [('prepared', 94, 0), ('committed', 95, 1)]:
            file = root / (action + '.db')
            assert subprocess.run([str(pending), 'init', str(file)], capture_output=True, timeout=10).returncode == 0
            assert subprocess.run([str(pending), action, str(file)], capture_output=True, timeout=10).returncode == exit_code
            source = Path(str(file) + '.source.db')
            source.unlink()
            Path(str(source) + '.energy-recovery.json').unlink()
            originals = [file, Path(str(file) + '.billing-recovery')]
            before = [p.read_bytes() for p in originals]
            read = subprocess.run(command + ['source-inspect', str(file)], capture_output=True, timeout=10)
            assert read.returncode == 0 and read.stdout.split() == [b'0', b'2', str(sequence).encode()]
            assert [p.read_bytes() for p in originals] == before
            checks += 4
    for path in (reader, library):
        content = path.read_bytes()
        for marker in (b'fixture.billing-query-hang.db', b'fixture.billing-query-change.db', b'BillingRecoveryTestPeer'):
            assert marker not in content
            checks += 1
    print(f'billing query lifecycle: {checks} checks passed; parent-bound pages, source-offline pending views and seam exclusion')


if __name__ == '__main__':
    main()
