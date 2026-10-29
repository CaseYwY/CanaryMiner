"""Owned synthetic compute parent death preserves both read sources and never stages money."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from test_forecast_lifecycle import process_identity, wait


def main():
    probe, fixture, library, reader = [Path(p).resolve(strict=True) for p in sys.argv[1:5]]
    checks = 0

    def check(value):
        nonlocal checks
        assert value
        checks += 1

    with tempfile.TemporaryDirectory(prefix='canary-billing-compute-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        parent = subprocess.Popen([str(probe), str(fixture), str(root)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        close = lambda: None
        try:
            marker = root / 'fixture.compute-source.db.fixture-ready'
            check(wait(lambda: marker.exists() and marker.read_text().strip().isdigit()))
            alive, close = process_identity(int(marker.read_text()))
            check(alive() and parent.poll() is None)
            files = [root / name for name in ('fixture.compute-source.db', 'fixture.compute-source.db.energy-recovery.json', 'fixture.compute-bill.db', 'fixture.compute-bill.db.billing-recovery')]
            originals = [p.read_bytes() for p in files]
            parent.kill()
            parent.wait(timeout=5)
            check(wait(lambda: not alive()))
            check([p.read_bytes() for p in files] == originals)
            check(not list(root.glob('*.billing-input*')))
        finally:
            if parent.poll() is None:
                parent.kill()
                parent.wait(timeout=5)
            close()
            parent.stdout.close()
            parent.stderr.close()
    for artifact in (library, reader):
        check(b'BillingComputeTestPeer' not in artifact.read_bytes())
    print(f'billing compute lifecycle: {checks} checks; owned helper death, preserved original stores, no monetary staging')


if __name__ == '__main__':
    main()
