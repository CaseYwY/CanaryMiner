"""Independent parent reconstruction, pending/receipt audit and ordinary-artifact seam checks."""
from contextlib import closing
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile


def main():
    probe, worker, fixture, library = (Path(p).resolve(strict=True) for p in sys.argv[1:5])
    checks = 0

    def check(condition):
        nonlocal checks
        assert condition
        checks += 1

    def run(file, action, helper=worker):
        result = subprocess.run([str(probe), str(helper), str(file), action], capture_output=True, encoding='utf8', timeout=10)
        check(result.returncode == 0)
        return dict(part.split('=', 1) for part in result.stdout.split())

    def facts(file):
        with closing(sqlite3.connect(file)) as db:
            return db.execute('SELECT binding,sequence,digest FROM meta').fetchone(), db.execute('SELECT body FROM events ORDER BY sequence').fetchall()

    def canonical(value):
        return json.dumps(value, separators=(',', ':'), ensure_ascii=False)

    def seal(record):
        body = canonical(record[:-1])[:-1]
        record[-1] = hashlib.sha256(body.encode()).hexdigest()
        return canonical(record).encode()

    with tempfile.TemporaryDirectory(prefix='canary-energy-recovery-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        for action, sequence, state in [('prepared', 0, 2), ('committed', 1, 2), ('acklost', 1, 3)]:
            file = root / (action + '.db')
            side = Path(str(file) + '.energy-recovery.json')
            check(run(file, 'init')['recovery'] == '1')
            check(run(file, action, fixture) == {'unknown': '1'})
            original, before = side.read_bytes(), facts(file)
            saved = json.loads(original)
            check(saved[0] == 'CMEJ1' and saved[1] == state and saved[3] == 0)
            check(seal(saved) == original)
            check(saved[5][0] == 'CMEQ1' and saved[5][1:3] == [1, 1] and saved[5][9] == [])
            check(saved[5][7] == saved[5][8][3])  # original observation, never restart/review time
            seed = hashlib.sha256(before[0][0].encode()).hexdigest()
            check(saved[4] == seed and before[0][1] == sequence)
            if sequence:
                check(canonical(saved[5][8]) == before[1][0][0])
                check(before[0][2] == hashlib.sha256((seed + '|' + before[1][0][0]).encode()).hexdigest())
            inspect = run(file, 'inspect')
            check(inspect['recovery'] == str(state) and inspect['sequence'] == str(sequence))
            owner = run(file, 'owner')
            check(owner['owner'] == ('2' if state == 3 else '4') and owner['pending'] == ('0' if state == 3 else '1'))
            check(side.read_bytes() == original and facts(file) == before)
            if state == 2:
                retry = run(file, 'retry')
                check(retry == {'owner': '2', 'recovery': '3', 'pending': '0', 'sequence': '1'})
            check(run(file, 'owner') == {'owner': '2', 'recovery': '3', 'pending': '0', 'sequence': '1'})
            check(len(facts(file)[1]) == 1)
            if os.name != 'nt':
                check(side.stat().st_mode & 0o777 == 0o600)

        file = root / 'guards.db'
        side = Path(str(file) + '.energy-recovery.json')
        run(file, 'init')
        run(file, 'prepared', fixture)
        original, before = side.read_bytes(), facts(file)
        edits = [
            lambda j: j.__setitem__(0, 'CMEJ2'),
            lambda j: j.__setitem__(1, 0),
            lambda j: j.__setitem__(1, 6),
            lambda j: j.__setitem__(2, 0),
            lambda j: j.__setitem__(3, 1),
            lambda j: j.__setitem__(4, '0' * 64),
            lambda j: j[5].__setitem__(0, 'CMEQ2'),
            lambda j: j[5].__setitem__(1, 2),
            lambda j: j[5].__setitem__(2, 2),
            lambda j: j[5].__setitem__(3, str(root / 'other.db')),
            lambda j: j[5][4][6].__setitem__(0, 'fixture.other'),
            lambda j: j[5][5].__setitem__(0, 4095),
            lambda j: j[5].__setitem__(6, 1),
            lambda j: j[5].__setitem__(7, j[5][7] + 1),
            lambda j: j[5].__setitem__(8, None),
            lambda j: j[5][8].__setitem__(1, 2),
            lambda j: j[5][8].__setitem__(3, 0),
            lambda j: j[5].__setitem__(9, [[1800000000000, 1800000001000, '', '']]),
        ]
        for edit in edits:
            record = json.loads(original)
            edit(record)
            changed = seal(record)
            side.write_bytes(changed)
            result = run(file, 'inspect')
            check(result['opened'] != '0' and result['recovery'] == '6')
            check(side.read_bytes() == changed and facts(file) == before)
        for changed in [original + b' ', original[:-1], b'[]', b' ' * 66049, original.replace(b'CMEJ1', b'CMEJ9')]:
            side.write_bytes(changed)
            check(run(file, 'inspect')['opened'] != '0')
            check(side.read_bytes() == changed and facts(file) == before)
        side.unlink()
        check(run(file, 'inspect')['opened'] != '0')
        check(not side.exists() and facts(file) == before)
        side.write_bytes(original)
        file.unlink()
        check(run(file, 'inspect')['opened'] != '0')
        check(not file.exists() and side.read_bytes() == original)

        for artifact in (worker, library):
            content = artifact.read_bytes()
            for seam in (b'fixture.exit-prepared', b'fixture.exit-committed', b'fixture.stall-before', b'fixture.bad-ack', b'CANARY_ENERGY_RECOVERY_TESTING'):
                check(seam not in content)
    print(f'energy recovery crash: {checks} checks; separate parent reconstruction, exact pending/receipt audit, preservation and seam exclusion passed')


if __name__ == '__main__':
    main()
