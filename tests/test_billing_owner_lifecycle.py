"""Kill only synthetic owned parents; verify helper lifetime and readonly restart independently."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from test_forecast_lifecycle import process_identity, wait


def main():
    probe, stage, monetary, writer, retry, fixture, library = [Path(p).resolve(strict=True) for p in sys.argv[1:8]]
    checks = 0

    def check(condition):
        nonlocal checks
        assert condition
        checks += 1

    def command(file, action):
        return [str(probe), str(stage), str(monetary), str(writer), str(retry), str(fixture), str(file), action]

    with tempfile.TemporaryDirectory(prefix='canary-billing-owner-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        for name, action in [('fixture.billing-owner-read-hang.db', 'read-parent'), ('fixture.billing-owner-late.db', 'write-parent')]:
            file = root / name
            parent = subprocess.Popen(command(file, action), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            close = lambda: None
            try:
                marker = Path(str(file) + '.fixture-ready')
                check(wait(lambda: marker.exists() and marker.read_text().strip().isdigit()))
                alive, close = process_identity(int(marker.read_text()))
                check(alive() and parent.poll() is None)
                originals = [file, Path(str(file) + '.billing-recovery'), Path(str(file) + '.billing-input')]
                before = [p.read_bytes() if p.exists() else None for p in originals]
                check(all(value is not None for value in before) if action == 'write-parent' else before == [None, None, None])
                parent.kill()
                parent.wait(timeout=5)
                check(wait(lambda: not alive()))
                check([p.read_bytes() if p.exists() else None for p in originals] == before)
                result = subprocess.run(command(file, 'inspect'), capture_output=True, timeout=15)
                check(result.returncode == 0)
                decoded = json.loads(result.stdout)
                check(decoded['stable'] and decoded['ticket'] == 1 and decoded['sequence'] == 0)
                check(decoded['stage'] and decoded['money'] == 1 if action == 'write-parent' else not decoded['stage'] and decoded['money'] == 0)
                check([p.read_bytes() if p.exists() else None for p in originals] == before)
            finally:
                if parent.poll() is None:
                    parent.kill()
                    parent.wait(timeout=5)
                close()
                parent.stdout.close()
                parent.stderr.close()
    for artifact in (library, stage, monetary, writer, retry):
        raw = artifact.read_bytes()
        for seam in (b'BillingOwnerTestPeer', b'fixture.billing-owner-late.db', b'fixture.billing-owner-read-hang.db'):
            check(seam not in raw)
    print(f'billing owner lifecycle: {checks} checks passed; owned parent/helper death, frozen originals, readonly restart and ordinary seam exclusion')


if __name__ == '__main__':
    main()
