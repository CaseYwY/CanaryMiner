"""Owned CPU helper parent death and production-artifact seam exclusion; no native sampling."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from test_forecast_lifecycle import process_identity, wait


def main():
    probe, fixture, native, library, core = [Path(p).resolve(strict=True) for p in sys.argv[1:6]]
    with tempfile.TemporaryDirectory(prefix='canary-safety-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        helper = root / ('fixture_safety' + ('.exe' if os.name == 'nt' else ''))
        shutil.copy2(fixture, helper)
        parent = subprocess.Popen([str(probe), str(helper), 'parent'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            marker = root / 'fixture-safety-ready'
            assert wait(lambda: marker.exists() and marker.read_text().strip().isdigit())
            alive, close = process_identity(int(marker.read_text()))
            try:
                assert alive() and parent.poll() is None
                parent.kill()
                parent.wait(timeout=5)
                assert wait(lambda: not alive())
            finally:
                close()
        finally:
            if parent.poll() is None:
                parent.kill()
            parent.wait(timeout=5)
            parent.stdout.close()
            parent.stderr.close()
    for artifact in (native, library, core):
        data = artifact.read_bytes()
        for name in (b'safetyTelemetryTestSymbol', b'safetyTelemetryOwnerTestSample', b'fixture-safety-ready', b'nvmlDeviceSet', b'nvmlDeviceGetHandleByIndex', b'cudaGetDevice',
                     b'nvmlDeviceReset', b'nvmlDeviceRemoveGpu', b'nvmlDeviceDiscoverGpus'):
            assert name not in data
    print('safety telemetry lifecycle: 30 parent-death and native-artifact checks passed')


if __name__ == '__main__':
    main()
