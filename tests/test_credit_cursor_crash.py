"""Independent process death before COMMIT and cursor-chain corruption on invented private scratch databases."""
import hashlib
from contextlib import closing
import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import sys
import tempfile


def main():
    probe = str(Path(sys.argv[1]).resolve(strict=True))
    checks = 0

    def run(file, action, expected=0):
        nonlocal checks
        result = subprocess.run([probe, str(file), action], capture_output=True, encoding='utf8', timeout=10)
        assert result.returncode == expected, (action, result.returncode, result.stdout, result.stderr)
        checks += 1
        return result.stdout

    def facts(file):
        with closing(sqlite3.connect(file)) as db:
            return (db.execute('SELECT sequence,source_asof,observed,last_batch FROM meta').fetchall(),
                    db.execute('SELECT * FROM snapshots ORDER BY sequence').fetchall(),
                    db.execute('SELECT * FROM records ORDER BY id').fetchall(),
                    db.execute('SELECT * FROM versions ORDER BY id,revision').fetchall(),
                    db.execute('SELECT * FROM coverage ORDER BY start_ms').fetchall())

    with tempfile.TemporaryDirectory(prefix='canary-cursor-crash-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        file = root / 'atomic.db'
        assert 'quantity=4.25 sequence=1 next=fixture:page-1 observed=1800000000000' in run(file, 'init')
        before = facts(file)
        run(file, 'crash', 17)
        assert facts(file) == before  # WAL recovery cannot commit either the corrected amount or the next cursor.
        assert 'quantity=4.25 sequence=1 next=fixture:page-1' in run(file, 'query')
        assert 'quantity=4.25 sequence=1 next=fixture:page-1' in run(file, 'rollback')
        assert facts(file) == before
        assert 'quantity=3.75 sequence=2 next=fixture:page-2 observed=1800000001000' in run(file, 'correct')
        assert 'quantity=3.75 sequence=2 next=fixture:page-2' in run(file, 'query')
        with closing(sqlite3.connect(file)) as db:
            assert db.execute('PRAGMA user_version').fetchone() == (2,)
            assert db.execute('SELECT amount FROM records').fetchall() == [('3.75',)]
            assert db.execute('SELECT count(*) FROM versions').fetchone() == (2,)
            assert db.execute('SELECT cursor_before,cursor_after FROM snapshots WHERE sequence=2').fetchone() == ('fixture:page-1', 'fixture:page-2')
        checks += 8

        base = root / 'base.db'
        run(base, 'init')
        run(base, 'empty')
        run(base, 'third')
        assert 'quantity=4.25 sequence=3 next=fixture:page-3' in run(base, 'query')

        def alter_last(db, field, value):
            batch = json.loads(db.execute('SELECT last_batch FROM meta').fetchone()[0])
            batch[6][field] = value
            db.execute('UPDATE meta SET last_batch=?', (json.dumps(batch, separators=(',', ':')),))

        def duplicate_target(db):
            # Remove schema constraints to exercise the independent bounded chain audit, not just SQLite UNIQUE.
            db.executescript('CREATE TABLE replacement AS SELECT * FROM snapshots; DROP TABLE snapshots; ALTER TABLE replacement RENAME TO snapshots;')
            db.execute("UPDATE snapshots SET cursor_after='fixture:page-1' WHERE sequence=3")
            alter_last(db, 1, 'fixture:page-1')

        corruptions = [
            lambda db: db.execute("UPDATE snapshots SET cursor_before='wrong' WHERE sequence=2"),
            lambda db: db.execute("UPDATE snapshots SET cursor_after='wrong' WHERE sequence=3"),
            lambda db: db.execute("UPDATE snapshots SET cursor_after='fixture:initial' WHERE sequence=1"),
            lambda db: db.execute("UPDATE snapshots SET cursor_after=? WHERE sequence=2", ('x' * 513,)),
            lambda db: db.execute("UPDATE snapshots SET cursor_after=? WHERE sequence=2", ('bad\ncontrol',)),
            lambda db: db.execute("UPDATE snapshots SET observed=-1 WHERE sequence=2"),
            lambda db: db.execute("UPDATE snapshots SET source_asof=1800000005000 WHERE sequence=2"),
            lambda db: db.execute("UPDATE snapshots SET observed=1799999999999 WHERE sequence=2"),
            lambda db: db.execute("UPDATE snapshots SET id='invalid id' WHERE sequence=2"),
            lambda db: db.execute("UPDATE snapshots SET sequence=4 WHERE sequence=2"),
            lambda db: db.execute('DELETE FROM snapshots WHERE sequence=1'),
            lambda db: db.execute('UPDATE meta SET sequence=400001'),
            lambda db: db.execute('PRAGMA user_version=1'),
            lambda db: db.execute("UPDATE meta SET last_batch='[]'"),
            lambda db: alter_last(db, 0, 'wrong'),
            lambda db: alter_last(db, 1, 'wrong'),
            duplicate_target,
        ]
        for index, corruption in enumerate(corruptions):
            target = root / f'corrupt-{index}.db'
            shutil.copyfile(base, target)
            with closing(sqlite3.connect(target)) as db, db:
                corruption(db)
            digest = hashlib.sha256(target.read_bytes()).digest()
            assert 'open=credit storage unavailable/integrity failure' in run(target, 'query', 1)
            assert target.exists() and hashlib.sha256(target.read_bytes()).digest() == digest, ('damaged fixture changed', index)
            checks += 1
    print(f'credit cursor crash: {checks} checks; atomic rollback, empty-page audit and preserved corruption passed')


if __name__ == '__main__':
    main()
