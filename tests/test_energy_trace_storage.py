"""Independent read-only source audits. Every database/identity/measurement here is invented."""
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
    probe = str(Path(sys.argv[1]).resolve(strict=True))
    checks = 0

    def run(file, action, version):
        nonlocal checks
        result = subprocess.run([probe, action, str(file), str(version)], capture_output=True, text=True, timeout=10, check=True)
        checks += 1
        return json.loads(result.stdout)

    with tempfile.TemporaryDirectory(prefix='canary-energy-trace-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        for version in (1, 2):
            file = root / f'fixture-{version}.db'
            assert run(file, 'inspect', version) == {'status': 8} and not file.exists()
            first = run(file, 'create', version)
            assert first['sequence'] == 2 and first['compacted'] == (2 if version == 2 else 0)
            assert first['watts'] == '123.456' and first['numerator'] == '123456'
            original = file.read_bytes()
            assert run(file, 'inspect', version) == first and file.read_bytes() == original
            assert run(file, 'inspect', 3 - version) == {'status': 8} and file.read_bytes() == original
            mutations = [
                "PRAGMA application_id=0",
                "PRAGMA user_version=99",
                "UPDATE meta SET digest='invalid'",
                "UPDATE meta SET sequence=1",
                "CREATE TABLE unexpected(id INTEGER)",
                "CREATE VIEW unexpected AS SELECT * FROM meta",
                "UPDATE retention SET seal='invalid'" if version == 2 else "UPDATE events SET body='[]' WHERE sequence=1",
                "UPDATE retention SET snapshot='[]'" if version == 2 else "UPDATE events SET digest='invalid' WHERE sequence=2",
            ]
            for index, sql in enumerate(mutations):
                damaged = root / f'damaged-{version}-{index}.db'
                shutil.copyfile(file, damaged)
                with closing(sqlite3.connect(damaged)) as db, db:
                    db.execute(sql)
                before = hashlib.sha256(damaged.read_bytes()).digest()
                assert run(damaged, 'inspect', version) == {'status': 8}
                assert hashlib.sha256(damaged.read_bytes()).digest() == before
                checks += 1
            checks += 4
        for suffix in ('-wal', '-shm', '-journal'):
            file = root / f'orphan{suffix}.db'
            companion = Path(str(file) + suffix)
            companion.write_bytes(b'fixture retained companion')
            assert run(file, 'create', 1) == {'status': 8} and not file.exists()
            assert companion.read_bytes() == b'fixture retained companion'
            checks += 1
    print(f'energy trace storage: {checks} independent checks passed; v1/v2 readonly audit and originals preserved')


if __name__ == '__main__':
    main()
