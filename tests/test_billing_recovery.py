"""Separate-process crash and independent SQLite corruption checks for synthetic monetary state."""
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
    probe, library = (str(Path(p).resolve()) for p in sys.argv[1:3])
    checks = 0

    def run(file, action, code=0):
        nonlocal checks
        result = subprocess.run([probe, action, str(file)], capture_output=True, text=True, timeout=10)
        assert result.returncode == code, (action, result.returncode, result.stderr)
        checks += 1
        return json.loads(result.stdout) if code == 0 else None

    def facts(file):
        with closing(sqlite3.connect(file)) as db:
            return db.execute('SELECT * FROM meta').fetchall(), db.execute('SELECT * FROM events ORDER BY sequence').fetchall()

    with tempfile.TemporaryDirectory(prefix='canary-billing-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        file = root / 'fixture.db'
        assert run(file, 'inspect')['opened'] == 10 and not file.exists()
        assert run(file, 'create')['sequence'] == 1
        before = facts(file)
        run(file, 'crash', 92)
        assert facts(file) == before
        assert run(file, 'inspect')['sequence'] == 1
        assert run(file, 'append')['sequence'] == 2
        settled = facts(file)
        replay = run(file, 'append')
        assert replay['applied'] == 1 and replay['settled'] and facts(file) == settled
        assert run(file, 'mismatch')['opened'] == 4 and facts(file) == settled
        checks += 5
        # Rehashing an incorrect derived monetary state still fails the independent arithmetic replay.
        def rehash_bad_state(db):
            binding, = db.execute('SELECT binding FROM meta').fetchone()
            parent = hashlib.sha256(binding.encode()).hexdigest()
            for seq, body, state in db.execute('SELECT sequence,body,state FROM events ORDER BY sequence').fetchall():
                changed = json.loads(state)
                changed[7] = '0'
                saved = json.dumps(changed, separators=(',', ':'))
                parent = hashlib.sha256((parent + '|' + body + '|' + saved).encode()).hexdigest()
                db.execute('UPDATE events SET state=?,digest=? WHERE sequence=?', (saved, parent, seq))
            db.execute('UPDATE meta SET state=?,digest=?', (saved, parent))

        mutations = [
            lambda db: db.execute("UPDATE events SET digest='invalid' WHERE sequence=1"),
            lambda db: db.execute("UPDATE events SET body='[]' WHERE sequence=1"),
            lambda db: db.execute("UPDATE events SET state='[]' WHERE sequence=2"),
            lambda db: db.execute("UPDATE meta SET state='[]'"),
            lambda db: db.execute("UPDATE meta SET digest='invalid'"),
            lambda db: db.execute('UPDATE meta SET sequence=1'),
            lambda db: db.execute('DELETE FROM events WHERE sequence=1'),
            lambda db: db.execute("UPDATE events SET id='other' WHERE sequence=2"),
            lambda db: db.execute('PRAGMA user_version=2'),
            lambda db: db.execute('PRAGMA application_id=0'),
            lambda db: db.execute('CREATE TABLE unexpected(id INTEGER)'),
            lambda db: db.execute('CREATE VIEW unexpected AS SELECT * FROM meta'),
            rehash_bad_state,
        ]
        for index, mutation in enumerate(mutations):
            target = root / f'damaged-{index}.db'
            shutil.copyfile(file, target)
            with closing(sqlite3.connect(target)) as db, db:
                mutation(db)
            before = hashlib.sha256(target.read_bytes()).digest()
            assert run(target, 'inspect')['opened'] == 10, index
            assert hashlib.sha256(target.read_bytes()).digest() == before, index
            checks += 1
        truncated = root / 'truncated.db'
        truncated.write_bytes(b'SQLite format 3\0')
        assert run(truncated, 'inspect')['opened'] == 10 and truncated.read_bytes() == b'SQLite format 3\0'
        checks += 1
    assert b'crashBeforeCommit' not in Path(library).read_bytes()
    checks += 1
    print(f'billing recovery: {checks} checks passed; no partial commit, exact replay, preserved corruption, ordinary seam absence')


if __name__ == '__main__':
    main()
