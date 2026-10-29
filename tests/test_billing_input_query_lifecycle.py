"""Readonly staged-original reconstruction in independent parents; no journal/ledger adoption or mutation."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from test_forecast_lifecycle import process_identity, wait


def main():
    probe, writer, write_fixture, retry, reader, stalled, library = [Path(p).resolve(strict=True) for p in sys.argv[1:8]]
    checks = 0

    def check(condition):
        nonlocal checks
        assert condition
        checks += 1

    def run(file, action, mode='real'):
        command = [str(probe), str(writer), str(write_fixture), str(retry), str(file), action, mode]
        if action == 'read-stage':
            command.append(str(reader))
        result = subprocess.run(command, capture_output=True, timeout=20)
        assert result.returncode == 0, (action, result.returncode, result.stderr)
        check(True)
        return json.loads(result.stdout)

    with tempfile.TemporaryDirectory(prefix='canary-stage-read-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        for name, action, mode, phase, outcome in [('receiving.db', 'stage', 'real', 1, False),
                                                   ('fixture.billing-input-prepared.db', 'submit', 'fixture', 2, False),
                                                   ('confirmed.db', 'submit', 'real', 3, True)]:
            file = root / name
            check(run(file, 'init')['status'] == 0)
            run(file, action, mode)
            check(not Path(str(file) + '.source.db').exists())
            originals = [file, Path(str(file) + '.billing-recovery'), Path(str(file) + '.billing-input')]
            before = [p.read_bytes() for p in originals]
            result = run(file, 'read-stage')
            check(result['status'] == 0 and result['phase'] == phase and result['original'] and result['outcome'] == outcome)
            check([p.read_bytes() for p in originals] == before)
        file = root / 'guards.db'
        check(run(file, 'create')['confirmed'])
        stage = Path(str(file) + '.billing-input')
        original = stage.read_bytes()
        monetary = [file, Path(str(file) + '.billing-recovery')]
        before = [p.read_bytes() for p in monetary]
        for invalid in [original + b' ', original[:-1], original.replace(b'CMBI1\n', b'CMBI8\n', 1), b'CMBI1\n9999999999\n',
                        b'x' * (1024 * 1024 + 128 * 1024 + 32 * 1024 + 16 * 1024 + 129)]:
            stage.write_bytes(invalid)
            result = run(file, 'read-stage')
            check(result['status'] == 10 and not result['original'] and not result['outcome'])
            check(stage.read_bytes() == invalid and [p.read_bytes() for p in monetary] == before)
        stage.write_bytes(original)
        temporary = Path(str(stage) + '.tmp')
        temporary.write_bytes(b'fixture unfinished replacement')
        result = run(file, 'read-stage')
        check(result['status'] == 0 and result['temporary'] and stage.read_bytes() == original and temporary.read_bytes() == b'fixture unfinished replacement')
        stage.unlink()
        result = run(file, 'read-stage')
        check(result['status'] == 0 and result['phase'] == 0 and result['temporary'] and not result['outcome'] and not stage.exists())
        check(temporary.read_bytes() == b'fixture unfinished replacement' and [p.read_bytes() for p in monetary] == before)
        if os.name != 'nt':
            target = root / 'fixture-preserved-target'
            target.write_bytes(original)
            stage.symlink_to(target)
            check(run(file, 'read-stage')['status'] == 10 and target.read_bytes() == original)
            stage.unlink()
            os.mkfifo(stage)
            check(run(file, 'read-stage')['status'] == 10)
            stage.unlink()
        file = root / 'fixture.billing-stage-query-hang.db'
        command = [str(probe), str(writer), str(write_fixture), str(retry), str(file), 'parent-stage', 'real', str(stalled)]
        parent = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        close = lambda: None
        try:
            marker = Path(str(file) + '.fixture-ready')
            check(wait(lambda: marker.exists() and marker.read_text().strip().isdigit()))
            alive, close = process_identity(int(marker.read_text()))
            check(alive() and parent.poll() is None)
            originals = [file, Path(str(file) + '.billing-recovery'), Path(str(file) + '.billing-input')]
            before = [p.read_bytes() for p in originals]
            parent.kill()
            parent.wait(timeout=5)
            check(wait(lambda: not alive()))
            check([p.read_bytes() for p in originals] == before)
            check(run(file, 'read-stage')['outcome'] and [p.read_bytes() for p in originals] == before)
        finally:
            if parent.poll() is None:
                parent.kill()
                parent.wait(timeout=5)
            close()
            parent.stdout.close()
            parent.stderr.close()
    for artifact in (reader, library):
        raw = artifact.read_bytes()
        for seam in (b'fixture.billing-stage-query-hang.db', b'fixture.billing-stage-query-change.db', b'BillingRecoveryTestPeer'):
            check(seam not in raw)
    print(f'billing input query lifecycle: {checks} checks passed; private restart reconstruction, preserved ambiguity and owned parent death')


if __name__ == '__main__':
    main()
