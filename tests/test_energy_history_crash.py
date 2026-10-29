"""Synthetic process death, independent SQLite audit and ordinary-artifact test-seam exclusion."""
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
    probe, worker, library = (Path(p).resolve(strict=True) for p in sys.argv[1:4])
    checks = 0

    def run(file, action, expected=0):
        nonlocal checks
        result = subprocess.run([str(probe), str(file), action], capture_output=True, encoding='utf8', timeout=10)
        assert result.returncode == expected, (action, result.returncode, result.stdout, result.stderr)
        checks += 1
        return result.stdout

    def facts(file):
        with closing(sqlite3.connect(file)) as db:
            return (db.execute('SELECT * FROM meta').fetchall(), db.execute('SELECT * FROM events ORDER BY sequence').fetchall())

    def change_event(db, sequence, edit):
        body = json.loads(db.execute('SELECT body FROM events WHERE sequence=?', (sequence,)).fetchone()[0])
        edit(body)
        db.execute('UPDATE events SET body=? WHERE sequence=?', (json.dumps(body, separators=(',', ':')), sequence))
        # Recompute corruption checksums to reach the independent semantic audit. These are not authentication.
        previous = hashlib.sha256(db.execute('SELECT binding FROM meta').fetchone()[0].encode()).hexdigest()
        for seq, text in db.execute('SELECT sequence,body FROM events ORDER BY sequence').fetchall():
            previous = hashlib.sha256((previous + '|' + text).encode()).hexdigest()
            db.execute('UPDATE events SET digest=? WHERE sequence=?', (previous, seq))
        db.execute('UPDATE meta SET digest=?', (previous,))

    with tempfile.TemporaryDirectory(prefix='canary-energy-history-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        file = root / 'atomic.db'
        assert 'sequence=1 numerator=323456 covered=3000 windows=3' in run(file, 'init')
        before = facts(file)
        run(file, 'crash', 86)
        assert facts(file) == before
        assert 'sequence=1 numerator=323456 covered=3000 windows=3' in run(file, 'query')
        run(file, 'after', 87)
        assert 'sequence=2 numerator=423456 covered=4000 windows=3' in run(file, 'query')
        after = facts(file)
        assert 'status=exact committed batch replay sequence=2' in run(file, 'append')
        assert facts(file) == after
        with closing(sqlite3.connect(file)) as db:
            assert db.execute('PRAGMA user_version').fetchone() == (1,)
            assert db.execute('PRAGMA application_id').fetchone() == (0x434d4548,)
            binding, seq, head = db.execute('SELECT binding,sequence,digest FROM meta').fetchone()
            previous = hashlib.sha256(binding.encode()).hexdigest()
            total_mw_ms = 0
            last = {}
            for number, text, digest in db.execute('SELECT sequence,body,digest FROM events ORDER BY sequence'):
                previous = hashlib.sha256((previous + '|' + text).encode()).hexdigest()
                assert digest == previous
                batch = json.loads(text)
                assert batch[1] == number
                for channel, _, mono, epoch, mw in batch[6]:
                    if channel in last:
                        prior, continuity, power = last[channel]
                        if power is not None and epoch == continuity and 0 < mono - prior <= 5000:
                            total_mw_ms += power * (mono - prior)
                    last[channel] = mono, epoch, mw
            assert seq == 2 and head == previous and total_mw_ms == 423456000
        if os.name != 'nt':
            assert file.stat().st_mode & 0o777 == 0o600
        checks += 9
        corruptions = [
            lambda db: db.execute('PRAGMA user_version=2'),
            lambda db: db.execute('PRAGMA application_id=1'),
            lambda db: db.execute("UPDATE meta SET digest='bad'"),
            lambda db: db.execute('UPDATE meta SET sequence=3'),
            lambda db: db.execute('DELETE FROM events WHERE sequence=1'),
            lambda db: db.execute("UPDATE events SET digest='bad' WHERE sequence=1"),
            lambda db: db.execute("UPDATE events SET body=body||' ' WHERE sequence=1"),
            lambda db: db.execute("UPDATE events SET body='[]' WHERE sequence=1"),
            lambda db: db.execute("UPDATE events SET id='wrong' WHERE sequence=1"),
            lambda db: db.execute('CREATE TABLE extra(x)'),
            lambda db: db.execute('CREATE VIEW extra AS SELECT * FROM events'),
            lambda db: db.execute('CREATE TRIGGER extra AFTER INSERT ON events BEGIN DELETE FROM meta; END'),
            lambda db: change_event(db, 2, lambda b: b.__setitem__(1, 3)),
            lambda db: change_event(db, 2, lambda b: b.__setitem__(3, 1799999999999)),
            lambda db: change_event(db, 2, lambda b: b.__setitem__(5, 'fixture.wrong-session')),
            lambda db: change_event(db, 2, lambda b: b[6][0].__setitem__(0, 64)),
            lambda db: change_event(db, 2, lambda b: b[6][0].__setitem__(2, 2100)),
            lambda db: change_event(db, 2, lambda b: b[6][0].__setitem__(4, -1)),
            lambda db: change_event(db, 2, lambda b: b[6][0].__setitem__(4, 4294967296)),
            lambda db: change_event(db, 1, lambda b: b[7][1].__setitem__(3, 1800000000001)),
            lambda db: change_event(db, 1, lambda b: b[7][2].__setitem__(6, '1')),
            lambda db: change_event(db, 1, lambda b: b[7][1].__setitem__(1, 'period-1')),
            lambda db: db.executescript('CREATE TABLE replacement AS SELECT * FROM events; DROP TABLE events; ALTER TABLE replacement RENAME TO events;'),
        ]
        for index, corruption in enumerate(corruptions):
            target = root / f'corrupt-{index}.db'
            shutil.copyfile(file, target)
            with closing(sqlite3.connect(target)) as db, db:
                corruption(db)
            original = target.read_bytes()
            assert 'open=energy storage/integrity unavailable' in run(target, 'query', 1)
            assert target.read_bytes() == original, ('damaged fixture changed', index)
            checks += 1
        missing = root / 'missing.db'
        run(missing, 'query', 1)
        assert not missing.exists()
        empty = root / 'empty.db'
        empty.write_bytes(b'')
        run(empty, 'query', 1)
        assert empty.read_bytes() == b''
        for artifact in (worker, library):
            content = artifact.read_bytes()
            for seam in (b'fixture.stall-before', b'fixture.stall-after', b'fixture-energy-stall-open', b'fixture.bad-ack', b'crashBeforeCommit'):
                assert seam not in content, ('test seam in ordinary artifact', artifact.name)
                checks += 1
    print(f'energy history crash: {checks} checks; atomic observations/windows, replay, corruption preservation and seam exclusion passed')


if __name__ == '__main__':
    main()
