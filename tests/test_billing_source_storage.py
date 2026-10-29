"""Synthetic monetary/source receipts: atomic crash, independent rehashed corruption and offline readonly replay."""
from contextlib import closing
import hashlib
import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import sys
import tempfile


def main():
    probe, library = (Path(p).resolve(strict=True) for p in sys.argv[1:3])
    checks = 0

    def run(file, source, action, code=0):
        nonlocal checks
        result = subprocess.run([str(probe), action, str(file), str(source)], capture_output=True, text=True, timeout=10)
        assert result.returncode == code, (action, result.returncode, result.stderr)
        checks += 1
        return json.loads(result.stdout) if code == 0 else None

    def facts(file):
        with closing(sqlite3.connect(file)) as db:
            return db.execute('SELECT * FROM meta').fetchall(), db.execute('SELECT * FROM events ORDER BY sequence').fetchall()

    def canonical(value):
        return json.dumps(value, separators=(',', ':'), ensure_ascii=False)

    with tempfile.TemporaryDirectory(prefix='canary-billing-source-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        bill, source = root / 'bill.db', root / 'source.db'
        assert run(bill, source, 'inspect')['opened'] == 10 and not bill.exists()
        first = run(bill, source, 'create')
        assert first['sequence'] == first['sourceSequence'] == 1
        before = facts(bill)
        run(bill, source, 'crash', 92)
        assert facts(bill) == before and run(bill, source, 'inspect')['sourceSequence'] == 1
        # The independent source COMMIT survives the monetary crash; explicit append imports it once.
        with closing(sqlite3.connect(source)) as db:
            assert db.execute('SELECT sequence FROM meta').fetchone() == (2,)
        second = run(bill, source, 'append')
        assert second['sequence'] == second['sourceSequence'] == 2
        committed = facts(bill)
        assert run(bill, source, 'replay')['applied'] == 1 and facts(bill) == committed
        checks += 5
        with closing(sqlite3.connect(bill)) as db:
            assert db.execute('PRAGMA user_version').fetchone() == (2,)
            binding = json.loads(db.execute('SELECT binding FROM meta').fetchone()[0])
            source_binding = json.loads(binding[7])
            seed = hashlib.sha256(canonical(source_binding[4]).encode()).hexdigest()
            body = json.loads(db.execute('SELECT body FROM events WHERE sequence=2').fetchone()[0])
            assert body[0] == 'CMBC2' and body[6][0].startswith('CMETQ2\n1\n') and body[6][1].startswith('CMETA2\n')
        checks += 2

        def rehash(db, mutation):
            bound, = db.execute('SELECT binding FROM meta').fetchone()
            parent = hashlib.sha256(bound.encode()).hexdigest()
            for seq, body, state in db.execute('SELECT sequence,body,state FROM events ORDER BY sequence').fetchall():
                parsed = json.loads(body)
                if seq == 2:
                    mutation(parsed)
                body = canonical(parsed)
                parent = hashlib.sha256((parent + '|' + body + '|' + state).encode()).hexdigest()
                db.execute('UPDATE events SET body=?,digest=? WHERE sequence=?', (body, parent, seq))
            db.execute('UPDATE meta SET digest=?', (parent,))

        def seed_reset(body):
            request = body[6][0].split('\n', 5)
            request[3:5] = ['0', seed]
            body[6][0] = '\n'.join(request)

        def wrong_source(body):
            request = body[6][0].split('\n', 5)
            inner = json.loads(request[5])
            inner[3] += '.other'
            request[5] = canonical(inner)
            body[6][0] = '\n'.join(request)

        def no_receipt(body):
            body[0] = 'CMBC1'
            del body[6]

        mutations = [
            seed_reset,
            wrong_source,
            no_receipt,
            lambda body: body[5][0].__setitem__(2, '1'),
            lambda body: body[6].__setitem__(1, body[6][1].replace('CMETA2\n', 'CMETA1\n', 1)),
            lambda body: body.__setitem__(3, body[3] + 1),
        ]
        for index, mutation in enumerate(mutations):
            damaged = root / f'damaged-{index}.db'
            shutil.copyfile(bill, damaged)
            with closing(sqlite3.connect(damaged)) as db, db:
                rehash(db, mutation)
            original = damaged.read_bytes()
            assert run(damaged, source, 'inspect')['opened'] == 10
            assert damaged.read_bytes() == original
            checks += 1
        for version in (1, 3):
            damaged = root / f'version-{version}.db'
            shutil.copyfile(bill, damaged)
            with closing(sqlite3.connect(damaged)) as db, db:
                db.execute(f'PRAGMA user_version={version}')
            original = damaged.read_bytes()
            assert run(damaged, source, 'inspect')['opened'] == 10 and damaged.read_bytes() == original
            checks += 1
        source.unlink()
        Path(str(source) + '.energy-recovery.json').unlink()
        assert run(bill, source, 'inspect')['digest'] == second['digest'] and facts(bill) == committed and not source.exists()
        checks += 1
    assert b'crashBeforeCommit' not in library.read_bytes()
    checks += 1
    print(f'billing source storage: {checks} checks passed; atomic receipts, independent ancestry corruption, offline audit')


if __name__ == '__main__':
    main()
