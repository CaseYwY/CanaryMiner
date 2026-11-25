"""Verify original meter review archiving through the ordinary CLI."""
from concurrent.futures import ThreadPoolExecutor
from hashlib import sha256
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
HEADER = b'CANARY-METER-REVIEW-1\n'
END = 1711929600000  # UTC midnight at the monthly reset
HOUR = 3600000
SCOPE = 'synthetic-review-private-scope'


def frames(data):
    assert data.startswith(HEADER)
    cutoff, tail = data[len(HEADER):].split(b'\n', 1)
    parts = []
    for _ in range(3):
        length, tail = tail.split(b'\n', 1)
        length = int(length)
        parts.append(tail[:length])
        tail = tail[length:]
    assert tail == sha256(data[:-65]).hexdigest().encode('ascii') + b'\n'
    return int(cutoff), parts


def pack(cutoff, parts):
    body = HEADER + str(cutoff).encode('ascii') + b'\n'
    body += b''.join(str(len(part)).encode('ascii') + b'\n' + part for part in parts)
    return body + sha256(body).hexdigest().encode('ascii') + b'\n'


def main(cli):
    with tempfile.TemporaryDirectory(prefix='canary-meter-review-') as name:
        root = Path(name)
        plan = json.loads((ROOT / 'config/tariff-declaration.example.json').read_bytes())
        begin = 1709251200000  # previous monthly reset
        plan.update(schema_version=2, confirmed_utc=begin // 1000,
                    reset_policy={'monthly_day': 1, 'clock': {'kind': 'utc', 'fixed_offset_seconds': 0}})
        plan['plan']['tiers'] = 'none'
        plan['plan']['revisions'][0].update(
            begin_utc_ms=begin, end_utc_ms=1714521600000, published_utc_ms=begin,
            time_mode='constant', base_rate_per_kwh='0.22', charge_per_civil_day='0',
            calendar=[], dynamic=[], bands=[])
        plan['opening'].update(scope=SCOPE, cycle_begin_utc_ms=begin, cycle_end_utc_ms=END,
                               through_utc_ms=END-HOUR, imported_kwh='0', accrued_subtotal='0')
        meter = {'schema_version': 1, 'scope': SCOPE, 'intervals': [
            {'begin_utc_ms': END-HOUR, 'end_utc_ms': END+HOUR, 'average_watts': '1000'}]}
        tariff_path = root / 'private rate.json'
        meter_path = root / 'private intervals.json'
        tariff = b'\xef\xbb\xbf' + json.dumps(plan, indent=2).encode('utf-8') + b'\r\n'
        intervals = json.dumps(meter).encode('utf-8') + b'\r\n'
        tariff_path.write_bytes(tariff)
        meter_path.write_bytes(intervals)
        inputs = ('--tariff', tariff_path.name, '--intervals', meter_path.name)
        cutoff = END + 2 * HOUR

        def run(*args):
            result = subprocess.run([str(cli), 'meter-bill', *map(str, args)], capture_output=True,
                                    cwd=root, text=True, timeout=10)
            for private in (SCOPE, 'private rate.json', 'private intervals.json', 'private-request',
                            'FORBIDDEN_DEVICE_SETUP'):
                assert private not in result.stdout + result.stderr
            return result

        def reject(result, text='', code=3):
            assert result.returncode == code and not result.stdout and text in result.stderr, result

        def save(target, explicit=True):
            as_of = ('--as-of-utc-ms', cutoff) if explicit else ()
            return run(*inputs, *as_of, '--save-review', target)

        for args in (('--open-review',), ('--open-review', 'missing.cmr', '--as-of-utc-ms', cutoff),
                     ('--open-review', 'missing.cmr', '--save-review', 'output.cmr'),
                     (*inputs, '--open-review', 'missing.cmr'),
                     (*inputs, '--save-review', '../private-request.cmr'),
                     (*inputs, '--save-review', 'CON.cmr'),
                     (*inputs, '--save-review', 'private-request.cmr:stream'),
                     (*inputs, '--save-review', 'repeat.cmr', '--save-review', 'repeat.cmr')):
            reject(run(*args), 'invalid options', 2)
        reject(run('--open-review', 'missing.cmr'), 'archive unavailable')
        assert sorted(entry.name for entry in root.iterdir()) == ['private intervals.json', 'private rate.json']

        # A continuous reading crosses the reset, and the archive retains both exact cycles and original bytes.
        saved = save('bill.cmr')
        assert saved.returncode == 0 and not saved.stderr, saved
        assert saved.stdout.count('Imported usage 1.000000 kWh') == 2
        assert f'Review cutoff {cutoff} UTC ms' in saved.stdout
        assert saved.stdout == run(*inputs, '--as-of-utc-ms', cutoff).stdout
        path = root / 'bill.cmr'
        original = path.read_bytes()
        stored_cutoff, parts = frames(original)
        assert (stored_cutoff, parts[0], parts[1]) == (cutoff, tariff, intervals)
        exact_rows = [json.loads(row) for row in parts[2].splitlines()]
        assert len(exact_rows) == 2
        assert exact_rows[0][5] == exact_rows[1][5] == '3600000000'
        assert exact_rows[0][10] is not None and exact_rows[1][10] is None
        if os.name != 'nt':
            assert stat.S_IMODE(path.stat().st_mode) & 0o077 == 0
        reject(save(path.name), 'preserved')
        assert path.read_bytes() == original

        # Existing inputs are dispensable on reopen. A private Unicode input filename must survive Windows argv.
        renamed = root / 'private-\u8d26\u5355-\u20ac.cmr'
        path.rename(renamed)
        tariff_path.unlink()
        meter_path.unlink()
        reopened = run('--open-review', renamed.name)
        assert (reopened.returncode, reopened.stderr, reopened.stdout) == (0, '', saved.stdout)
        assert renamed.read_bytes() == original
        reject(run('--open-review', renamed.name, '--intervals', meter_path.name), 'invalid options', 2)

        bad = root / 'invalid.cmr'
        for corrupt in (original[:-1], original[:-2] + (b'1\n' if original[-2:-1] == b'0' else b'0\n'),
                        original.replace(HEADER, b'CANARY-METER-REVIEW-2\n', 1),
                        original.replace(HEADER, HEADER+b'0', 1),
                        HEADER + b'1\n999999999999\n',
                        b'x' * (2*1024*1024)):
            bad.write_bytes(corrupt)
            reject(run('--open-review', bad.name), 'archive unavailable')
        # Changing the hash along with the result or input cannot bypass the arithmetic replay.
        altered_result = parts[2].replace(b'"3600000000"', b'"3600000001"', 1)
        bad.write_bytes(pack(cutoff, [parts[0], parts[1], altered_result]))
        reject(run('--open-review', bad.name), 'differ from the recalculated amounts')
        subtle_input = parts[1].replace(b'"1000"', b'"1000.00000000001"', 1)
        bad.write_bytes(pack(cutoff, [parts[0], subtle_input, parts[2]]))
        reject(run('--open-review', bad.name), 'differ from the recalculated amounts')
        bad.write_bytes(pack(int(time.time()*1000) + 2*HOUR, parts))
        reject(run('--open-review', bad.name), 'review cutoff is in the future')
        bad.write_bytes(pack(END, parts))
        reject(run('--open-review', bad.name), 'interval ends after the review cutoff')

        tariff_path.write_bytes(tariff)
        meter_path.write_bytes(intervals)
        original_hold = root / 'held.cmr.tmp'
        original_hold.write_bytes(b'owned by an earlier invocation')
        reject(save('held.cmr'), 'preserved')
        assert original_hold.read_bytes() == b'owned by an earlier invocation'
        assert not (root / 'held.cmr').exists()
        if os.name != 'nt':
            link = root / 'linked.cmr'
            link.symlink_to(renamed.name)
            reject(save(link.name), 'preserved')
            reject(run('--open-review', link.name), 'archive unavailable')
            assert link.is_symlink() and renamed.read_bytes() == original
            fifo = root / 'fifo.cmr'
            os.mkfifo(fifo)
            reject(run('--open-review', fifo.name), 'archive unavailable')
        with ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(save, ['race.cmr'] * 4))
        assert sorted(result.returncode for result in results) == [0, 3, 3, 3]
        assert (root / 'race.cmr').read_bytes() == original
        assert sorted(entry.name for entry in root.glob('*.tmp')) == [original_hold.name]

        bad_meter = dict(meter, scope='private-mismatched-meter')
        meter_path.write_text(json.dumps(bad_meter), encoding='utf-8')
        reject(save('rejected.cmr'), 'meter scope differs')
        assert not (root / 'rejected.cmr').exists() and not (root / 'rejected.cmr.tmp').exists()

        # Exercise the archive size above the old 1 MiB generic file-read ceiling.
        tariff_path.write_bytes(tariff)
        meter_path.write_bytes(intervals.ljust(1024*1024, b' '))
        last = save('largest.cmr', explicit=False)
        assert last.returncode == 0, last
        large_bytes = (root / 'largest.cmr').read_bytes()
        assert len(large_bytes) > 1024*1024
        assert frames(large_bytes)[1][1] == meter_path.read_bytes()
        reopen_large = run('--open-review', 'largest.cmr')
        assert (reopen_large.returncode, reopen_large.stderr, reopen_large.stdout) == (0, '', last.stdout)
    print('meter-bill archive: exact originals, deterministic replay, corruption refusal, sizes and concurrent no-overwrite')


if __name__ == '__main__':
    main(Path(sys.argv[1]).resolve())
