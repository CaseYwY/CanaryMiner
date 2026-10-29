"""Real retry helpers, durable exact originals, owned parent death, and lost acknowledgements without live sources."""
from contextlib import closing
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
from test_forecast_lifecycle import process_identity, wait


def main():
    probe, worker, fixture, pending, library = [Path(p).resolve(strict=True) for p in sys.argv[1:6]]
    checks = 0

    def run(args, code=0):
        nonlocal checks
        result = subprocess.run([str(a) for a in args], capture_output=True, timeout=15)
        assert result.returncode == code, (result.returncode, result.stderr)
        checks += 1
        return json.loads(result.stdout) if result.stdout else None

    def facts(file):
        with closing(sqlite3.connect(file)) as db:
            return db.execute('SELECT * FROM meta').fetchall(), db.execute('SELECT * FROM events ORDER BY sequence').fetchall()

    with tempfile.TemporaryDirectory(prefix='canary-billing-retry-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        for name, mode, expected, sequence in [('normal.db', 'real', 0, 1), ('fixture.billing-retry-precommit.db', 'fixture', None, 0),
                                              ('fixture.billing-retry-committed.db', 'fixture', None, 1), ('fixture.billing-retry-lost.db', 'fixture', None, 1)]:
            file = root / name
            side = Path(str(file) + '.billing-recovery')
            assert run([pending, 'init', file])['state'] == 1
            run([pending, 'prepared', file], 94)
            original = side.read_bytes()
            source = Path(str(file) + '.source.db')
            source.unlink()
            Path(str(source) + '.energy-recovery.json').unlink()
            result = run([probe, worker, fixture, file, mode])
            assert result['applied'] == expected and result['confirmed'] == (expected == 0)
            current = run([pending, 'inspect', file])
            assert current['sequence'] == sequence
            if name in ('fixture.billing-retry-precommit.db', 'fixture.billing-retry-committed.db'):
                assert current['state'] == 2 and side.read_bytes() == original
            else:
                assert current['state'] == 3
            checks += 3
            resumed = run([probe, worker, fixture, file, 'real'])
            assert resumed['confirmed'] and resumed['applied'] == (0 if sequence == 0 else 1) and resumed['sequence'] == 1
            assert len(facts(file)[1]) == 1 and not source.exists()
            checks += 2
        file = root / 'failed.db'
        side = Path(str(file) + '.billing-recovery')
        run([pending, 'init', file])
        run([pending, 'failed-retry', file], 94)
        original, before = side.read_bytes(), facts(file)
        failed = run([probe, worker, fixture, file, 'real'])
        assert failed['applied'] == 5 and not failed['confirmed'] and side.read_bytes() == original and facts(file) == before
        checks += 1
        file = root / 'fixture.billing-retry-hang.db'
        parent = subprocess.Popen([str(probe), str(worker), str(fixture), str(file), 'parent-probe'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        close = lambda: None
        try:
            marker = Path(str(file) + '.fixture-ready')
            assert wait(lambda: marker.exists() and marker.read_text().strip().isdigit())
            alive, close = process_identity(int(marker.read_text()))
            assert alive() and parent.poll() is None
            side = Path(str(file) + '.billing-recovery')
            before, original = facts(file), side.read_bytes()
            parent.kill()
            parent.wait(timeout=5)
            assert wait(lambda: not alive())
            assert facts(file) == before and side.read_bytes() == original
            checks += 4
        finally:
            if parent.poll() is None:
                parent.kill()
                parent.wait(timeout=5)
            close()
            parent.stdout.close()
            parent.stderr.close()
    for artifact in (worker, library):
        content = artifact.read_bytes()
        for seam in (b'fixture.billing-retry-precommit.db', b'fixture.billing-retry-committed.db', b'fixture.billing-retry-hang.db', b'BillingRecoveryTestPeer'):
            assert seam not in content
            checks += 1
    print(f'billing retry crash: {checks} checks passed; exact isolated recovery, before/after commit, lost proof and owned parent death')


if __name__ == '__main__':
    main()
