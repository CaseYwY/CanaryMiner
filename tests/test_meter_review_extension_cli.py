"""Continue a private meter review through the ordinary CLI."""
from concurrent.futures import ThreadPoolExecutor
import copy
from hashlib import sha256
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from test_meter_bill_archive_cli import frames, pack
from test_meter_bill_cli import APRIL, MARCH, MAY, ROOT, SCOPE


HOUR = 3_600_000
JUNE = 1717200000000
HEADER = b'CANARY-METER-REVIEW-4\n'


def interval(begin, end, watts='1000'):
    return {'begin_utc_ms': begin, 'end_utc_ms': end, 'average_watts': watts}


def main(cli):
    with tempfile.TemporaryDirectory(prefix='canary-meter-continuation-') as directory:
        root = Path(directory)
        tariff, source, invoice = [root / name for name in ('private declaration.json', 'private meter.json', 'private invoice.json')]
        prior = root / 'private-\u8d26\u5355.cmr'
        first = APRIL - 2 * HOUR
        plan = json.loads((ROOT / 'config/tariff-declaration.example.json').read_bytes())
        plan.update(schema_version=2, confirmed_utc=MARCH // 1000,
                    reset_policy={'monthly_day': 1, 'clock': {'kind': 'utc', 'fixed_offset_seconds': 0}})
        plan['plan'].update(tiers='none', cycle_charge='0', subtotal_multiplier='1')
        plan['plan']['revisions'][0].update(begin_utc_ms=MARCH, end_utc_ms=JUNE, published_utc_ms=MARCH,
                                           time_mode='constant', base_rate_per_kwh='0.22',
                                           charge_per_civil_day='0', calendar=[], bands=[])
        plan['opening'].update(scope=SCOPE, cycle_begin_utc_ms=MARCH, cycle_end_utc_ms=APRIL, through_utc_ms=first)
        rows = [interval(first, first + HOUR), interval(first + HOUR, APRIL),
                interval(APRIL, APRIL + HOUR), interval(APRIL + HOUR, MAY), interval(MAY, MAY + HOUR)]
        readings = {'schema_version': 1, 'scope': SCOPE, 'intervals': rows}
        count = 0

        def write(path, payload):
            path.write_text(json.dumps(payload), encoding='utf-8')

        def run(*args, code=0, message=''):
            nonlocal count
            count += 1
            result = subprocess.run([cli, 'meter-bill', *map(str, args)], cwd=root, capture_output=True,
                                    text=True, timeout=12)
            assert result.returncode == code, (args, result)
            assert all(private not in result.stdout + result.stderr for private in
                       (SCOPE, 'private declaration.json', 'private meter.json', 'private invoice.json',
                        'FORBIDDEN_DEVICE_SETUP')), result
            if code:
                assert not result.stdout and message in result.stderr, result
            else:
                assert result.stdout and not result.stderr, result
            return result.stdout

        def save(target, cutoff, *extra):
            return run('--tariff', tariff, '--intervals', source, '--as-of-utc-ms', cutoff,
                       '--save-review', target, *extra)

        def extend(archive=prior, out='next.cmr', *extra, code=0, message=''):
            return run('--extend-review', archive, '--intervals', source, '--save-review', out,
                       *extra, code=code, message=message)

        def assert_missing(name):
            candidate = root / name
            assert not candidate.exists() and not candidate.with_suffix('.cmr.tmp').exists()

        write(tariff, plan)
        write(source, {**readings, 'intervals': rows[:1]})
        before = save('seed.cmr', first + HOUR)
        (root / 'seed.cmr').rename(prior)
        original = prior.read_bytes()
        tariff.unlink()  # Extension uses the exact tariff in the original archive.
        for args in (('--extend-review', prior), ('--extend-review', prior, '--save-review', 'next.cmr'),
                     ('--extend-review', prior, '--intervals', source),
                     ('--open-review', prior, '--extend-review', prior),
                     ('--extend-review', prior, '--intervals', source, '--tariff', tariff, '--save-review', 'next.cmr'),
                     ('--extend-review', prior, '--intervals', source, '--octopus-rates', source, '--save-review', 'next.cmr'),
                     ('--extend-review', prior, '--intervals', source, '--save-review', '../x.cmr')):
            run(*args, code=2, message='invalid options')

        extend(code=3, message='extend the saved meter coverage')
        assert_missing('next.cmr')
        write(source, {**readings, 'intervals': rows[:3]})
        extend(prior, 'next.cmr', '--as-of-utc-ms', first + HOUR - 1,
               code=3, message='precedes the saved cutoff')
        assert_missing('next.cmr')
        reviewed = extend(prior, 'next.cmr', '--as-of-utc-ms', APRIL + HOUR)
        assert 'Provisional cycle estimate 0.220000 USD' in before
        assert 'Cycle estimate at closing 0.440000 USD' in reviewed
        assert 'Provisional cycle estimate 0.220000 USD' in reviewed
        assert f'Extends verified meter coverage through {first + HOUR}' in reviewed
        assert sha256(original).hexdigest() in reviewed and prior.read_bytes() == original
        next_file = root / 'next.cmr'
        verified = next_file.read_bytes()
        assert verified.startswith(HEADER)
        assert run('--open-review', next_file) == reviewed

        for changed, problem in (
            ([{**rows[0], 'average_watts': '999'}, *rows[1:3]], 'differ from the saved review prefix'),
            ([interval(first, APRIL), rows[2]], 'extend the saved meter coverage'),
            ([rows[0], rows[2]], 'gap or overlap'),
        ):
            write(source, {**readings, 'intervals': changed})
            extend(prior, 'bad.cmr', code=3, message=problem)
            assert_missing('bad.cmr')
        write(source, readings)
        assert 'Cycle 3' in extend(next_file, 'third.cmr')
        assert next_file.read_bytes() == verified
        prior.unlink()
        assert run('--open-review', next_file) == reviewed
        assert run('--open-review', 'third.cmr').count('Cycle estimate at closing') == 2

        # A prefix checksum with a fresh valid outer checksum still has to match the replayed readings and totals.
        corrupt = root / 'corrupt.cmr'
        parts = verified[:-65].split(b'\n')
        lineage_index, = [i for i, part in enumerate(parts) if part == sha256(original).hexdigest().encode()]
        parts[lineage_index + 1] = b'0' * 64
        forged = b'\n'.join(parts)
        corrupt.write_bytes(forged + sha256(forged).hexdigest().encode() + b'\n')
        run('--open-review', corrupt, code=3, message='differ from the saved review prefix')
        extend(corrupt, 'corrupt-child.cmr', code=3, message='differ from the saved review prefix')
        assert_missing('corrupt-child.cmr')
        cutoff, parts = frames(original)
        parts[2] = parts[2].replace(b'"3600000000"', b'"3600000001"', 1)
        corrupt.write_bytes(pack(cutoff, parts))
        extend(corrupt, 'corrupt-child.cmr', code=3, message='saved review amounts differ')
        assert_missing('corrupt-child.cmr')

        # A new publication may change estimates for new readings only if the exact old amounts still match.
        late = copy.deepcopy(plan)
        late['confirmed_utc'] = APRIL // 1000
        rule = json.loads((ROOT / 'config/tariff-declaration.example.json').read_bytes())['plan']['revisions'][0]['calendar'][0]
        rule.update(begin_minute=0, end_minute=1440, published_utc_ms=APRIL, rate_per_kwh='0.44')
        late['plan']['revisions'][0].update(time_mode='calendar', calendar=[rule])
        write(tariff, late)
        write(source, {**readings, 'intervals': rows[:1]})
        save('late.cmr', APRIL - HOUR)
        write(source, {**readings, 'intervals': rows[:3]})
        extend('late.cmr', 'late-child.cmr', '--as-of-utc-ms', APRIL + HOUR, code=3,
               message='new cutoff would change the saved review charges')
        assert_missing('late-child.cmr')

        # The absolute initial reading is bound even when a shifted series has identical consumption deltas.
        write(tariff, plan)
        points = [{'utc_ms': at, 'imported_kwh': str(100 + i)}
                  for i, at in enumerate((first, first + HOUR, APRIL, APRIL + HOUR))]
        register = {'schema_version': 2, 'scope': SCOPE, 'register_readings': points}
        write(source, {**register, 'register_readings': points[:2]})
        save('register.cmr', first + HOUR)
        shifted = [{**row, 'imported_kwh': str(int(row['imported_kwh']) + 100)} for row in points]
        write(source, {**register, 'register_readings': shifted})
        extend('register.cmr', 'shifted.cmr', code=3, message='differ from the saved review prefix')
        assert_missing('shifted.cmr')
        write(source, {**readings, 'intervals': rows[:3]})
        extend('register.cmr', 'shifted.cmr', code=3, message='differ from the saved review prefix')
        assert_missing('shifted.cmr')
        write(source, register)
        assert 'Cycle estimate at closing 0.440000 USD' in extend('register.cmr', 'register-next.cmr')

        # Only one writer can publish a requested destination. Originals remain byte-for-byte intact.
        held = root / 'held.cmr.tmp'
        held.write_bytes(b'previous writer')
        extend('register.cmr', 'held.cmr', code=3, message='preserved')
        assert held.read_bytes() == b'previous writer' and not (root / 'held.cmr').exists()
        with ThreadPoolExecutor(max_workers=4) as pool:
            commands = [pool.submit(subprocess.run,
                        [cli, 'meter-bill', '--extend-review', 'register.cmr', '--intervals', source,
                         '--save-review', 'race.cmr'], cwd=root, capture_output=True, timeout=12)
                        for _ in range(4)]
        assert sorted(f.result().returncode for f in commands) == [0, 3, 3, 3]
        assert run('--open-review', 'race.cmr').startswith('Estimated electricity costs')
        assert not (root / 'race.cmr.tmp').exists()

        # Earlier invoices are verified as part of their own archive. A successor can retain a new full invoice.
        plan['opening']['through_utc_ms'] = MARCH
        write(tariff, plan)
        zero = [interval(MARCH, APRIL, '0'), interval(APRIL, MAY, '0')]
        write(source, {**readings, 'intervals': zero[:1]})
        supplied = {'schema_version': 1, 'scope': SCOPE, 'currency': 'USD', 'decimal_places': 2,
                    'rounding': 'half_even', 'cycles': [{'begin_utc_ms': MARCH, 'end_utc_ms': APRIL, 'supply_charge': '0.00'}]}
        write(invoice, supplied)
        assert '(match)' in save('invoiced.cmr', APRIL, '--invoice', invoice)
        old_invoice = (root / 'invoiced.cmr').read_bytes()
        write(source, {**readings, 'intervals': zero})
        assert 'Invoice comparison' not in extend('invoiced.cmr', 'without-invoice.cmr')
        supplied['cycles'].append({'begin_utc_ms': APRIL, 'end_utc_ms': MAY, 'supply_charge': '0.00'})
        write(invoice, supplied)
        final = extend('invoiced.cmr', 'with-invoice.cmr', '--invoice', invoice)
        assert final.count('(match)') == 2 and run('--open-review', 'with-invoice.cmr') == final
        assert (root / 'invoiced.cmr').read_bytes() == old_invoice
        run('--open-review', 'with-invoice.cmr', '--invoice', invoice, code=3, message='already contains')
    print(f'Meter review continuation passed {count} ordinary CLI and 4 competing publication checks.')


if __name__ == '__main__':
    main(Path(sys.argv[1]).resolve(strict=True))
