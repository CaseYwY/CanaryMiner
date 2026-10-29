"""Independent stage framing, CAS, source-offline commit/recovery, and owned-process lifetime tests."""
from contextlib import closing
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
from test_forecast_lifecycle import process_identity, wait


def main():
    probe, worker, fixture, retry, library = [Path(p).resolve(strict=True) for p in sys.argv[1:6]]
    checks = 0

    def check(condition):
        nonlocal checks
        assert condition
        checks += 1

    def run(file, action, mode='real'):
        result = subprocess.run([str(probe), str(worker), str(fixture), str(retry), str(file), action, mode], capture_output=True, timeout=20)
        assert result.returncode == 0, (action, result.returncode, result.stderr, result.stdout)
        check(True)
        return json.loads(result.stdout)

    def facts(file):
        with closing(sqlite3.connect(file)) as db:
            return db.execute('SELECT * FROM meta').fetchall(), db.execute('SELECT * FROM events ORDER BY sequence').fetchall()

    def canonical(value):
        return json.dumps(value, separators=(',', ':'), ensure_ascii=False).encode()

    def frame(value):
        return str(len(value)).encode() + b'\n' + value + b'\n'

    def encode(manifest, meta, payload):
        body = b'CMBI1\n' + frame(canonical(manifest)) + frame(canonical(meta)) + frame(payload)
        return body + hashlib.sha256(body).hexdigest().encode() + b'\n'

    def decode(raw):
        offset, values = 6, []
        assert raw[:offset] == b'CMBI1\n'
        for _ in range(3):
            end = raw.index(b'\n', offset)
            size = int(raw[offset:end])
            offset = end + 1
            values.append(raw[offset:offset + size])
            offset += size + 1
        assert raw[offset:] == hashlib.sha256(raw[:offset]).hexdigest().encode() + b'\n'
        result = (json.loads(values[0]), json.loads(values[1]), values[2])
        assert encode(*result) == raw
        return result

    with tempfile.TemporaryDirectory(prefix='canary-billing-input-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        for name, sequence, state in [('issued', 0, 1), ('prepared', 0, 2), ('precommit', 0, 2), ('committed', 1, 2), ('settled', 1, 3), ('lost', 1, 3)]:
            file = root / f'fixture.billing-input-{name}.db'
            stage, journal = Path(str(file) + '.billing-input'), Path(str(file) + '.billing-recovery')
            check(run(file, 'init')['status'] == 0)
            before = facts(file)
            result = run(file, 'submit', 'fixture')
            check(result['transport'] == 3 and not result['confirmed'] and result['issued'])
            frozen = stage.read_bytes()
            manifest, meta, payload = decode(frozen)
            check(meta[0] == (3 if name == 'lost' else 2))
            check(len(payload) == manifest[2] + manifest[3])
            check(hashlib.sha256(payload[:manifest[2]]).hexdigest() == manifest[4])
            check(hashlib.sha256(payload[manifest[2]:]).hexdigest() == manifest[5])
            check(not Path(str(file) + '.source.db').exists())
            current = run(file, 'query')
            check(current['sequence'] == sequence and current['state'] == state)
            originals, current_facts = journal.read_bytes(), facts(file)
            read = run(file, 'inspect')
            check(read['status'] == 0 and stage.read_bytes() == frozen and facts(file) == current_facts)
            # Neither a second Commit nor a fresh upload can repeat an issued input.
            check(run(file, 'commit')['status'] == 5 and journal.read_bytes() == originals and facts(file) == current_facts)
            if state == 2:
                check(run(file, 'reconcile')['status'] == 5 and stage.read_bytes() == frozen)
                check(run(file, 'retry')['confirmed'])
            check(run(file, 'reconcile')['phase'] == 3)
            final = decode(stage.read_bytes())
            check(final[0] == manifest and final[2] == payload and final[1][0] == 3)
            if name == 'issued':
                check(facts(file) == before and final[1][1][0:2] == [0, 5])
            else:
                check(len(facts(file)[1]) == 1 and final[1][1][0] == 3)
            if os.name != 'nt':
                check(stage.stat().st_mode & 0o777 == 0o600)

        # Creation uncertainty is not evidence of ownership of an identical empty pair.
        file = root / 'fixture.billing-input-created.db'
        stage = Path(str(file) + '.billing-input')
        check(run(file, 'create', 'fixture')['transport'] == 3)
        original, before = stage.read_bytes(), facts(file)
        check(decode(original)[1][0] == 2 and run(file, 'query')['state'] == 1)
        check(run(file, 'reconcile')['status'] == 5 and stage.read_bytes() == original and facts(file) == before)
        check(run(file, 'commit')['status'] == 5 and stage.read_bytes() == original)

        # A pre-replace interruption leaves one bounded original temporary slot, never adopted or overwritten.
        file = root / 'fixture.billing-input-temp.db'
        stage, temporary = Path(str(file) + '.billing-input'), Path(str(file) + '.billing-input.tmp')
        check(run(file, 'create', 'fixture')['transport'] == 3)
        original = temporary.read_bytes()
        check(not file.exists() and not stage.exists() and decode(original)[1] == [1, None])
        check(run(file, 'create')['status'] == 10 and temporary.read_bytes() == original and not stage.exists())

        # Lost page acknowledgement is readable, cannot become commit permission, and can be explicitly resumed.
        file = root / 'fixture.billing-input-page.db'
        run(file, 'init')
        check(run(file, 'submit', 'fixture')['transport'] == 3)
        stage = Path(str(file) + '.billing-input')
        original = stage.read_bytes()
        check(decode(original)[1] == [1, None] and run(file, 'query')['sequence'] == 0)
        check(run(file, 'inspect')['phase'] == 1 and stage.read_bytes() == original)
        check(run(file, 'commit')['phase'] == 3 and run(file, 'query')['sequence'] == 1)

        # Full-sized framed metadata mutations are rehashed independently; files remain untouched on refusal.
        file = root / 'guards.db'
        stage = Path(str(file) + '.billing-input')
        run(file, 'init')
        run(file, 'stage')
        original, before = stage.read_bytes(), facts(file)
        edits = [
            lambda m, s, p: (m, [0, None], p),
            lambda m, s, p: (m, [4, None], p),
            lambda m, s, p: (m, [3, None], p),
            lambda m, s, p: (m, s, p[:-1]),
            lambda m, s, p: (m, s, p + b' '),
            lambda m, s, p: (m, s, b'x' + p[1:]),
            lambda m, s, p: (m[:4] + ['f' * 64] + m[5:], s, p),
        ]
        for edit in edits:
            stage.write_bytes(encode(*edit(*decode(original))))
            changed = stage.read_bytes()
            check(run(file, 'inspect')['status'] != 0 and stage.read_bytes() == changed and facts(file) == before)
        stage.write_bytes(original)
        for changed in [original + b' ', original[:-1], original[:-65] + b'f' * 64 + b'\n']:
            stage.write_bytes(changed)
            check(run(file, 'inspect')['status'] != 0 and stage.read_bytes() == changed and facts(file) == before)
        stage.write_bytes(original)
        if os.name != 'nt':
            target = root / 'fixture-stage-target'
            target.write_bytes(original)
            stage.unlink()
            stage.symlink_to(target)
            check(run(file, 'inspect')['status'] != 0 and target.read_bytes() == original)
            stage.unlink()
            stage.write_bytes(original)
        file = root / 'fixture.billing-input-hang.db'
        parent = subprocess.Popen([str(probe), str(worker), str(fixture), str(retry), str(file), 'parent-probe', 'fixture'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        close = lambda: None
        try:
            marker = Path(str(file) + '.fixture-ready')
            check(wait(lambda: marker.exists() and marker.read_text().strip().isdigit()))
            alive, close = process_identity(int(marker.read_text()))
            check(alive() and parent.poll() is None)
            parent.kill()
            parent.wait(timeout=5)
            check(wait(lambda: not alive()))
            check(not file.exists() and not Path(str(file) + '.billing-input').exists())
        finally:
            if parent.poll() is None:
                parent.kill()
                parent.wait(timeout=5)
            close()
            parent.stdout.close()
            parent.stderr.close()
    for artifact in (worker, library):
        content = artifact.read_bytes()
        for seam in (b'fixture.billing-input-issued.db', b'fixture.billing-input-committed.db', b'fixture.billing-input-hang.db', b'BillingRecoveryTestPeer'):
            check(seam not in content)
    print(f'billing input crash: {checks} checks passed; bounded originals, explicit reconciliation, offline recovery and owned lifetime')


if __name__ == '__main__':
    main()
