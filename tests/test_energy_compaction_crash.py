"""Atomic exact-segment checkpoint/expiry and independent rechecksummed snapshot audit."""
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

    def check(value):
        nonlocal checks
        assert value
        checks += 1

    def run(file, action, code=0):
        result = subprocess.run([str(probe), str(file), action], capture_output=True, encoding='utf8', timeout=15)
        check(result.returncode == code)
        return dict(s.split('=', 1) for s in result.stdout.split())

    def facts(file):
        with closing(sqlite3.connect(file)) as db:
            return [db.execute('SELECT * FROM ' + table + ' ORDER BY 1').fetchall() for table in ['meta', 'events', 'retention']]

    def canonical(value):
        return json.dumps(value, separators=(',', ':'), ensure_ascii=False)

    with tempfile.TemporaryDirectory(prefix='canary-energy-compaction-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        file = root / 'atomic.db'
        check(run(file, 'init')['compacted'] == '0')
        before = facts(file)
        run(file, 'crash', 86)
        check(facts(file) == before)
        check(run(file, 'query')['sequence'] == '1')
        run(file, 'after', 87)
        check(run(file, 'query') == {'sequence': '2', 'compacted': '2', 'numerator': '423456', 'covered': '4000'})
        after = facts(file)
        check(not after[1] and after[0][0][2] == 2)
        binding = after[0][0][1]
        _, policy, body, seal = after[2][0]
        state = json.loads(body)
        check(state[0] == 'CMECS1' and state[1][:4] == [2, 8, 1, 3])
        check(seal == hashlib.sha256((binding + '|' + policy + '|' + body).encode()).hexdigest())
        check(state[2] == before[0][0][3])
        check(state[3] == after[0][0][3] == hashlib.sha256((state[2] + '|' + canonical(state[4])).encode()).hexdigest())
        check(state[11] == [[1800000000000, 1800000001000, 123456, 2], [1800000001000, 1800000002000, 200000, 1], [1800000002000, 1800000003000, 100000, 1]])
        check(sum((z - a) * mw for a, z, mw, _ in state[11]) == 423456000)
        check(sum((z - a) * count for a, z, _, count in state[11]) == 4000)
        first = json.loads(before[1][0][2])
        check(sorted(state[10][0][7]) == sorted(first[7]))
        check(run(file, 'append')['applied'] == '1' and facts(file) == after)
        with closing(sqlite3.connect(file)) as db:
            check(db.execute('PRAGMA user_version').fetchone() == (2,))
            check(db.execute('PRAGMA application_id').fetchone() == (0x434d4548,))

        edits = [
            lambda s: s.__setitem__(0, 'CMECS2'),
            lambda s: s[1].__setitem__(0, 3),
            lambda s: s[1].__setitem__(1, 257),
            lambda s: s[1].__setitem__(2, 2),
            lambda s: s[1].__setitem__(3, 2),
            lambda s: s[1].__setitem__(4, 'fixture.wrong'),
            lambda s: s[1].__setitem__(5, 'fixture.wrong'),
            lambda s: s[1].__setitem__(7, 1800000000001),
            lambda s: s.__setitem__(2, 'bad'),
            lambda s: s[5][6].__setitem__(1, s[5][6][0]),
            lambda s: s[5][6][0].__setitem__(2, 0),
            lambda s: s[5][6][0].__setitem__(4, 2),
            lambda s: s.__setitem__(6, 0),
            lambda s: s.__setitem__(7, 0),
            lambda s: s[9].__setitem__(0, 'fixture.wrong'),
            lambda s: s[10][0][7][2].__setitem__(3, 1800000000001),
            lambda s: s[11][0].__setitem__(3, 0),
            lambda s: s[11][0].__setitem__(3, 3),
            lambda s: s[11][0].__setitem__(2, 8589934591),
            lambda s: s[11][1].__setitem__(0, 1800000000001),
            lambda s: s[11][2].__setitem__(1, 1800000003001),
            lambda s: s[11].append(s[11][-1]),
        ]
        for n, edit in enumerate(edits):
            target = root / f'corrupt-{n}.db'
            shutil.copyfile(file, target)
            changed = json.loads(body)
            edit(changed)
            text = canonical(changed)
            checksum = hashlib.sha256((binding + '|' + policy + '|' + text).encode()).hexdigest()
            with closing(sqlite3.connect(target)) as db, db:
                db.execute('UPDATE retention SET snapshot=?,seal=?', (text, checksum))
            original = target.read_bytes()
            check(run(target, 'query', 1) == {'open': '8'})
            check(target.read_bytes() == original)
        for n, sql in enumerate(['DELETE FROM retention', "UPDATE retention SET seal='bad'", "UPDATE retention SET snapshot=snapshot||' '", 'PRAGMA user_version=1', 'CREATE TABLE extra(x)']):
            target = root / f'schema-{n}.db'
            shutil.copyfile(file, target)
            with closing(sqlite3.connect(target)) as db, db:
                db.execute(sql)
            original = target.read_bytes()
            check(run(target, 'query', 1) == {'open': '8'})
            check(target.read_bytes() == original)
        for artifact in (worker, library):
            content = artifact.read_bytes()
            for seam in [b'fixture.compact-prepared.db', b'fixture.compact-committed.db', b'fixture.compact-bad-ack.db', b'crashBeforeCommit']:
                check(seam not in content)
    print(f'energy compaction crash: {checks} checks; atomic expiry/checkpoint, exact independent integral, snapshot preservation and seam exclusion passed')


if __name__ == '__main__':
    main()
