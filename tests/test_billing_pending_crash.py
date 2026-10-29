"""Independent exact-original reconstruction and framed journal mutation, using only owned synthetic stores."""
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
    probe, library = (Path(p).resolve(strict=True) for p in sys.argv[1:3])
    checks = 0

    def check(condition):
        nonlocal checks
        assert condition
        checks += 1

    def run(file, action, code=0):
        p = subprocess.run([str(probe), action, str(file)], capture_output=True, encoding='utf8', timeout=15)
        assert p.returncode == code, (action, p.returncode, p.stderr)
        check(True)
        return json.loads(p.stdout) if code == 0 else None

    def facts(file):
        with closing(sqlite3.connect(file)) as db:
            return db.execute('SELECT binding,sequence,digest,state FROM meta').fetchone(), db.execute('SELECT body,digest,state FROM events ORDER BY sequence').fetchall()

    def canonical(value):
        return json.dumps(value, separators=(',', ':'), ensure_ascii=False).encode()

    def frame(value):
        return str(len(value)).encode() + b'\n' + value + b'\n'

    def encode(meta, binding, chunk):
        content = b'CMBJ1\n' + b''.join(frame(canonical(v)) for v in (meta, binding, chunk))
        return content + hashlib.sha256(content).hexdigest().encode() + b'\n'

    def decode(content):
        offset, values = 6, []
        assert content[:6] == b'CMBJ1\n'
        for _ in range(3):
            end = content.index(b'\n', offset)
            length = int(content[offset:end])
            offset = end + 1
            values.append(json.loads(content[offset:offset + length]))
            offset += length + 1
        assert content[offset:] == hashlib.sha256(content[:offset]).hexdigest().encode() + b'\n'
        assert encode(*values) == content
        return values

    def remove_source(file):
        source = Path(str(file) + '.source.db')
        source.unlink()
        Path(str(source) + '.energy-recovery.json').unlink()

    with tempfile.TemporaryDirectory(prefix='canary-billing-pending-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        for action, code, sequence, state in [('temp', 93, 0, 1), ('prepared', 94, 0, 2), ('precommit', 92, 0, 2),
                                              ('committed', 95, 1, 2), ('settled', 96, 1, 3), ('settle-failure', 0, 1, 2)]:
            file = root / (action + '.db')
            side = Path(str(file) + '.billing-recovery')
            check(run(file, 'init')['state'] == 1)
            empty, before = side.read_bytes(), facts(file)
            run(file, action, code)
            original, committed = side.read_bytes(), facts(file)
            metadata, bound, chunk = decode(original)
            check(metadata[0] == state and metadata[4] == 0 and metadata[5] == hashlib.sha256(canonical(bound)).hexdigest())
            check(committed[0][1] == sequence)
            check((not sequence and committed == before) or (sequence and canonical(chunk).decode() == committed[1][0][0]))
            if state != 1:
                check(chunk[0] == 'CMBC2' and chunk[6][0].startswith('CMETQ2\n1\n'))
                check(metadata[3] > chunk[3])  # query/prepare time does not replace source observation time
            remove_source(file)
            inspected = run(file, 'inspect')
            check(inspected['state'] == state and inspected['sequence'] == sequence)
            check(side.read_bytes() == original and facts(file) == committed)
            if state == 2:
                for invalid in ('retry-changed', 'retry-stale'):
                    result = run(file, invalid)
                    check(result['applied'] == 5 and result['state'] == 2 and side.read_bytes() == original and facts(file) == committed)
                result = run(file, 'retry')
                check(result['state'] == 3 and result['sequence'] == 1 and result['applied'] == (1 if sequence else 0))
                new_meta, new_binding, new_chunk = decode(side.read_bytes())
                check(new_meta[3:] == metadata[3:] and new_binding == bound and new_chunk == chunk)
                check(facts(file)[1][0][0] == canonical(chunk).decode() and len(facts(file)[1]) == 1)
            elif state == 1:
                check(original == empty and len(list(root.glob(side.name + '.tmp-*'))) == 1)
                check(run(file, 'inspect')['state'] == 1 and side.read_bytes() == original)
            else:
                check(run(file, 'retry')['applied'] == 1 and side.read_bytes() == original)
            if os.name != 'nt':
                check(side.stat().st_mode & 0o777 == 0o600)

        for action, code, state in [('failed-retry', 94, 2), ('rejected', 0, 4)]:
            file = root / (action + '.db')
            side = Path(str(file) + '.billing-recovery')
            run(file, 'init')
            run(file, action, code)
            original, before = side.read_bytes(), facts(file)
            check(run(file, 'inspect')['state'] == state)
            remove_source(file)
            result = run(file, 'retry')
            check(result['applied'] == 5 and result['state'] == state and side.read_bytes() == original and facts(file) == before)

        file = root / 'guards.db'
        side = Path(str(file) + '.billing-recovery')
        run(file, 'init')
        run(file, 'prepared', 94)
        original, before = side.read_bytes(), facts(file)
        edits = [
            lambda m, b, c: m.__setitem__(0, 0),
            lambda m, b, c: m.__setitem__(0, 5),
            lambda m, b, c: m.__setitem__(1, 0),
            lambda m, b, c: m.__setitem__(4, 1),
            lambda m, b, c: m.__setitem__(5, 'f' * 64),
            lambda m, b, c: m.__setitem__(2, m[2] + '.other'),
            lambda m, b, c: m.__setitem__(3, m[3] + 1),
            lambda m, b, c: m.__setitem__(3, c[3]),
            lambda m, b, c: b.__setitem__(1, 'fixture.other'),
            lambda m, b, c: b[6].__setitem__(0, 128),
            lambda m, b, c: c.__setitem__(1, 2),
            lambda m, b, c: c.__setitem__(2, ''),
            lambda m, b, c: c.__setitem__(3, m[3] + 1),
            lambda m, b, c: c[6].__setitem__(0, c[6][0].replace('CMETQ2', 'CMETQ1', 1)),
            lambda m, b, c: c[6].__setitem__(1, c[6][1] + ' '),
        ]
        for edit in edits:
            m, b, c = decode(original)
            edit(m, b, c)
            changed = encode(m, b, c)
            side.write_bytes(changed)
            result = run(file, 'inspect')
            check(result['opened'] != 0 and result['state'] == 0 and not result['original'])
            check(side.read_bytes() == changed and facts(file) == before)
        for changed in [original + b' ', original[:-1], original.replace(b'CMBJ1', b'CMBJ9', 1), b'CMBJ1\n9999999999\n',
                        b'CMBJ1\n0\n\n', original.replace(b'CMBJ1\n', b'CMBJ1\n0', 1), b' ' * (1024 * 1024 + 128 * 1024 + 16 * 1024 + 129)]:
            side.write_bytes(changed)
            check(run(file, 'inspect')['opened'] != 0)
            check(side.read_bytes() == changed and facts(file) == before)
        side.write_bytes(original)
        database = file.read_bytes()
        file.write_bytes(b'fixture corrupt monetary header')
        check(run(file, 'inspect')['opened'] != 0 and file.read_bytes() == b'fixture corrupt monetary header' and side.read_bytes() == original)
        file.write_bytes(database)
        m, b, c = decode(original)
        m[0:2] = [3, 0]  # forged Confirmed against the still-empty committed prefix is Diverged, not repaired
        changed = encode(m, b, c)
        side.write_bytes(changed)
        check(run(file, 'inspect')['state'] == 5 and side.read_bytes() == changed and facts(file) == before)
        check(run(file, 'retry')['applied'] == 5 and side.read_bytes() == changed)
        side.unlink()
        check(run(file, 'inspect')['opened'] != 0 and not side.exists() and facts(file) == before)
        check(run(file, 'init')['opened'] != 0 and not side.exists() and facts(file) == before)
        side.write_bytes(original)
        file.unlink()
        check(run(file, 'inspect')['opened'] != 0 and not file.exists() and side.read_bytes() == original)
        check(run(file, 'init')['opened'] != 0 and not file.exists() and side.read_bytes() == original)
        if os.name != 'nt':
            target = root / 'symlink-target'
            target.write_bytes(original)
            side.unlink()
            side.symlink_to(target)
            check(run(file, 'inspect')['opened'] != 0 and target.read_bytes() == original)

    content = library.read_bytes()
    for seam in (b'BillingRecoveryTestPeer', b'BillingRecoveryTestPoint', b'crashBeforeCommit'):
        check(seam not in content)
    print(f'billing pending crash: {checks} checks passed; exact offline retry, crash boundaries, framed originals and preserved failures')


if __name__ == '__main__':
    main()
