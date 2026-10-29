"""Independent canonical records, parent reconstruction, orphan preservation and production seam checks."""
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

    def run(file, action, helper=worker, now=1800000100000):
        result = subprocess.run([str(probe), '--probe', str(helper), str(file), action, str(now)], capture_output=True, encoding='utf8', timeout=10)
        check(result.returncode == 0 and result.stderr == '')
        return dict(part.split('=', 1) for part in result.stdout.split())

    def canonical(value):
        return json.dumps(value, separators=(',', ':'), ensure_ascii=False)

    def seal(record):
        body = canonical(record[:-1])[:-1]
        record[-1] = hashlib.sha256(body.encode()).hexdigest()
        return canonical(record).encode()

    def store(file):
        return file.parent / ('energy-history.' + json.loads(file.read_bytes())[6] + '.db')

    def facts(file):
        with closing(sqlite3.connect(file)) as db:
            return db.execute('SELECT binding,sequence,digest FROM meta').fetchone(), db.execute('SELECT body FROM events ORDER BY sequence').fetchall()

    with tempfile.TemporaryDirectory(prefix='canary-authorization-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        absent = root / 'absent.json'
        check(run(absent, 'load')['status'] == '1' and not absent.exists())
        for point, created in [('reserved', False), ('created', True)]:
            file = root / ('fixture-exit-' + point + '.json')
            check(run(file, 'enroll', fixture) == {'unknown': '1'})
            original = file.read_bytes()
            document = json.loads(original)
            check(seal(document) == original and document[0] == 'CMEP1')
            check(document[1] == 1 and document[5] == 1 and document[3][0] is False and document[8] == '')
            check(len(document[6]) == 32 and len(document[7][-1]) == 2)
            check(b'GPU-' not in original and b'fixture' not in original)
            database = store(file)
            check(database.exists() == created)
            before = facts(database) if created else None
            check(run(file, 'load')['enrollment'] == '1' and file.read_bytes() == original)
            check(run(file, 'enroll')['status'] == '3' and file.read_bytes() == original)
            check(run(file, 'inspect')['status'] == ('0' if created else '8'))
            check(file.read_bytes() == original and database.exists() == created)
            check(run(file, 'confirm')['enrollment'] == ('2' if created else '1'))
            check(database.exists() == created)
            if created:
                check(facts(database) == before)
                confirmed = json.loads(file.read_bytes())
                check(confirmed[1] == 2 and confirmed[6:8] == document[6:8] and confirmed[3][0] is False)
            if os.name != 'nt':
                check(file.stat().st_mode & 0o777 == 0o600)

        # Pending confirmation requires both original files and an exact empty prefix. It never recreates either.
        for remove in ['database', 'journal']:
            folder = root / remove
            folder.mkdir()
            file = folder / 'fixture-exit-created.json'
            check(run(file, 'enroll', fixture) == {'unknown': '1'})
            database = store(file)
            journal = Path(str(database) + '.energy-recovery.json')
            missing = database if remove == 'database' else journal
            missing.unlink()
            original = file.read_bytes()
            survivor = journal if remove == 'database' else database
            survivor_bytes = survivor.read_bytes()
            check(run(file, 'confirm')['status'] == '8' and not missing.exists())
            check(file.read_bytes() == original and survivor.read_bytes() == survivor_bytes)

        # An independent parent cannot turn an old last-issued ID into a new issuance receipt.
        file = root / 'fixture-exit-issued.json'
        check(run(file, 'enroll')['status'] == '0')
        check(run(file, 'enable')['status'] == '0')
        check(run(file, 'provision', fixture) == {'unknown': '1'})
        old = json.loads(file.read_bytes())
        check(old[8].startswith('session.') and len(old[8]) == 40)
        check(run(file, 'load')['issued'] == '0')
        check(run(file, 'provision')['issued'] == '1')
        new = json.loads(file.read_bytes())
        check(new[8] != old[8] and new[6:8] == old[6:8] and new[1] == old[1] + 1)
        check(facts(store(file))[0][1] == 0 and facts(store(file))[1] == [])
        original = file.read_bytes()
        edits = [
            lambda j: j.__setitem__(0, 'CMEP2'),
            lambda j: j.__setitem__(1, 0),
            lambda j: j.__setitem__(2, 0),
            lambda j: j[3].__setitem__(1, 499),
            lambda j: j[3].__setitem__(2, 999),
            lambda j: j[4].__setitem__(0, 4097),
            lambda j: j[4][5].__setitem__(1, 32769),
            lambda j: j.__setitem__(5, 1),  # Pending cannot carry consent/session issuance
            lambda j: j.__setitem__(6, '0' * 32),  # selected-set hash remains bound
            lambda j: j[7].__setitem__(3, 'selection.' + '0' * 64),
            lambda j: j.__setitem__(8, 'session-1'),
            lambda j: j.append(0),
        ]
        for edit in edits:
            record = json.loads(original)
            edit(record)
            corrupt = seal(record)
            file.write_bytes(corrupt)
            check(run(file, 'load')['status'] == '4' and file.read_bytes() == corrupt)
        file.write_bytes(original)
        check(run(file, 'load', now=1800000000000)['status'] == '5' and file.read_bytes() == original)
        # Fresh identities: small collision/shape smoke check, not a statistical or entropy certification.
        identities = {new[8]}
        for _ in range(10):
            check(run(file, 'provision')['issued'] == '1')
            identities.add(json.loads(file.read_bytes())[8])
        check(len(identities) == 11)
    for artifact in [worker, library]:
        data = artifact.read_bytes()
        for seam in [b'fixture-exit-reserved', b'fixture-exit-created', b'fixture-exit-issued', b'fixture-entropy-fail', b'fixture-stall-before', b'fixture-stall-after', b'fixture-bad-ack']:
            check(seam not in data)
    print(f'energy authorization reconstruction: {checks} checks passed')


if __name__ == '__main__':
    main()
