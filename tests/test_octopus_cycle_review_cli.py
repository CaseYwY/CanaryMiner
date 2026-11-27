"""Close adjacent Octopus billing cycles using separate captures and private originals."""
import copy
from decimal import Decimal
from hashlib import sha256
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from test_meter_bill_named_cli import BUNDLE, HOUR, SCOPE, named, plan, register, utc
from test_octopus_meter_rates_cli import GO_PRODUCT, HALF_HOUR, PAYMENT, PRODUCT, decimal_text, pages, row, timestamp


LONDON = named('Europe/London')
HEADER = b'CANARY-METER-REVIEW-6\n'


def cycle(begin, end, product, kwh, pence, standing, days, *, bundle=False):
    declaration = plan(begin, begin, end, LONDON, {'monthly_day': 1, 'clock': LONDON})
    declaration['plan'].update(key=f'octopus-example-{begin}', territory='C', supplier='Octopus Energy',
                               product=product, currency='GBP', rounding='meter_import_half_hour_kwh_half_even_2')
    declaration['plan']['revisions'][0].update(time_mode='dynamic', base_rate_per_kwh='0')
    times = list(range(begin, end + 1, HALF_HOUR))
    assert times[-1] == end
    if bundle:
        readings = dict(schema_version=3, scope=SCOPE, end_utc_ms=end,
                        observed_utc_ms=end + HOUR, consumption=[
                            dict(consumption=0.015, interval_start=timestamp(a), interval_end=timestamp(b))
                            for a, b in zip(times, times[1:])])
    else:
        readings = register([(at, decimal_text(kwh + Decimal('0.015') * i)) for i, at in enumerate(times)])
    units = ([row(begin - HOUR, end + HOUR, pence, PAYMENT)] if product == GO_PRODUCT else
             [row(a, b, pence) for a, b in zip(times, times[1:])])
    snapshot = dict(schema_version=1, product_code=product, tariff_code=f'E-1R-{product}-C',
                    payment_method=PAYMENT, observed_utc_ms=end + HOUR,
                    unit_rate_pages=pages(list(reversed(units)), size=71),
                    standing_charge_pages=pages([row(begin, None, standing)]))
    amount = (Decimal('0.02') * (len(times) - 1) * pence + days * standing) / 100
    return declaration, readings, snapshot, amount


def main(cli, bundle):
    with tempfile.TemporaryDirectory(prefix='canary-octopus-cycle-') as directory:
        root = Path(directory)
        installed = root / 'installed'
        installed.mkdir()
        binary = Path(shutil.copy2(cli, installed / cli.name))
        shutil.copytree(bundle, installed / BUNDLE)
        tariff, readings, rates, invoice = [root / name for name in
            ('private tariff \u7535.json', 'private imports.json', 'private public capture.json', 'private invoice.json')]
        count = 0

        def write(path, document):
            path.write_text(json.dumps(document, separators=(',', ':')), encoding='ascii')

        def inputs(c):
            for path, doc in zip((tariff, readings, rates), c):
                write(path, doc)

        def run(*args, code=0, reason=''):
            nonlocal count
            result = subprocess.run([binary, 'meter-bill', *map(str, args)], cwd=root, capture_output=True,
                                    text=True, timeout=20)
            assert result.returncode == code, (args, result)
            assert all(value not in result.stdout + result.stderr for value in
                       (SCOPE, tariff.name, rates.name, readings.name, invoice.name, 'FORBIDDEN_DEVICE_SETUP'))
            if code:
                assert not result.stdout and reason in result.stderr, result
            else:
                assert result.stdout and not result.stderr, result
            count += 1
            return result.stdout

        def save(target, cutoff, *extra):
            return run('--tariff', tariff, '--intervals', readings, '--octopus-rates', rates,
                       '--save-review', target, '--as-of-utc-ms', cutoff, *extra)

        def advance(previous, target, cutoff, *extra, **expectation):
            return run('--next-cycle-review', previous, '--tariff', tariff, '--intervals', readings,
                       '--octopus-rates', rates, '--save-review', target, '--as-of-utc-ms', cutoff,
                       *extra, **expectation)

        def close(amount, result):
            assert result.count('Cycle estimate at closing') == 1, result
            assert f'Cycle estimate at closing {amount:.6f} GBP' in result, result

        def refused(previous, cutoff, reason):
            advance(previous, 'rejected.cmr', cutoff, code=3, reason=reason)
            assert not (root / 'rejected.cmr').exists() and not (root / 'rejected.cmr.tmp').exists()

        march = utc('2023-03-01T00:00:00')
        april = utc('2023-03-31T23:00:00')
        may = utc('2023-04-30T23:00:00')
        june = utc('2023-05-31T23:00:00')
        assert (april - march) // HALF_HOUR == 1486
        previous = cycle(march, april, PRODUCT, Decimal(20), -10, 20, 31)
        current = cycle(april, may, PRODUCT, Decimal('42.290'), 35, 50, 30)
        following = cycle(may, june, PRODUCT, Decimal('63.890'), 0, 0, 31)
        inputs(previous)
        write(invoice, dict(schema_version=1, scope=SCOPE, currency='GBP', decimal_places=2, rounding='half_even',
                            cycles=[dict(begin_utc_ms=march, end_utc_ms=april, supply_charge='3.23')]))
        march_report = save('march.cmr', april + HOUR, '--invoice', invoice)
        close(previous[3], march_report)
        assert '(match)' in march_report
        old = root / 'march-\u7535.cmr'
        (root / 'march.cmr').rename(old)
        old_bytes = old.read_bytes()
        partial = copy.deepcopy(previous)
        partial[1]['register_readings'].pop()
        inputs(partial)
        assert 'Provisional cycle estimate' in save('partial.cmr', april + HOUR)

        inputs(current)
        refused('partial.cmr', may + HOUR, 'previous review must be a complete')
        refused(old, april + HOUR - 1, 'precedes the saved cutoff')
        refused(old, may + HOUR - 1, 'rates observed after')
        for flags in (('--next-cycle-review', old, '--intervals', readings, '--tariff', tariff, '--save-review', 'x.cmr'),
                      ('--next-cycle-review', old, '--intervals', readings, '--octopus-rates', rates, '--save-review', 'x.cmr'),
                      ('--previous-cycle', old, '--tariff', tariff, '--intervals', readings)):
            run(*flags, code=2, reason='invalid options')
        for modify, diagnostic in (
            (lambda c: c[1]['register_readings'].pop(), 'requires one complete'),
            (lambda c: [point.update(imported_kwh=decimal_text(Decimal(point['imported_kwh']) + 10))
                        for point in c[1]['register_readings']], 'opening register differs'),
            (lambda c: c[2].update(payment_method='NON_DIRECT_DEBIT'), 'payment method, meter or reset rule differs'),
            (lambda c: c[0]['reset_policy'].update(clock=dict(kind='fixed_standard', fixed_offset_seconds=3600)),
             'payment method, meter or reset rule differs'),
        ):
            bad = copy.deepcopy(current)
            modify(bad)
            inputs(bad)
            refused(old, may + HOUR, diagnostic)
        bad = copy.deepcopy(current)
        bad[0]['opening']['scope'] = bad[1]['scope'] = 'replacement-meter'
        inputs(bad)
        refused(old, may + HOUR, 'payment method, meter or reset rule differs')
        inputs(following)
        refused(old, june + HOUR, 'does not start at the previous closing time')

        inputs(current)
        source_bytes = [path.read_bytes() for path in (tariff, readings, rates)]
        write(invoice, dict(schema_version=1, scope=SCOPE, currency='GBP', decimal_places=2, rounding='half_even',
                            cycles=[dict(begin_utc_ms=april, end_utc_ms=may, supply_charge=str(current[3]))]))
        report = advance(old, 'april.cmr', may + HOUR, '--invoice', invoice)
        close(current[3], report)
        assert '(match)' in report and f'Previous Octopus cycle closed at {april}' in report
        old_hash = sha256(old_bytes).hexdigest()
        assert old_hash in report
        assert all(path.read_bytes() == data for path, data in zip((tariff, readings, rates), source_bytes))
        assert old.read_bytes() == old_bytes
        archived = root / 'april.cmr'
        recorded = archived.read_bytes()
        assert recorded.startswith(HEADER)
        binding = ['canary-octopus-cycle-1', 'whole_meter_import', 'declared_forecast',
                   'meter_import_half_hour_kwh_half_even_2']
        opening = current[0]['opening']
        binding.extend(json.dumps(value) for value in
                       (current[0]['country'], 'C', 'Octopus Energy', PRODUCT, 'GBP', PAYMENT, SCOPE,
                        opening['adapter'], '2026e'))
        binding.extend(['named_iana', '"Europe/London"', '0'] * 2)
        binding.extend(['2', '1', '0', '0', 'whole_day', ''])
        binding_hash = sha256('\n'.join(binding).encode('ascii')).hexdigest().encode('ascii')
        assert old_hash.encode() + b'\n' + binding_hash + b'\n' in recorded
        assert run('--open-review', archived, '--previous-cycle', old) == report
        assert run('--open-review', old) == march_report
        run('--open-review', old, '--previous-cycle', 'partial.cmr', code=3, reason='no previous cycle link')
        run('--open-review', archived, '--previous-cycle', 'partial.cmr', code=3, reason='checksum differs')
        advance(old, 'april.cmr', may + HOUR, code=3, reason='preserved')
        assert archived.read_bytes() == recorded

        body = recorded[:-65]
        fingerprint = old_hash.encode() + b'\n'
        assert body.count(fingerprint) == 1
        left, right = body.split(fingerprint)
        tampered = left + fingerprint + b'0' * 64 + right[64:]
        (root / 'tampered.cmr').write_bytes(tampered + sha256(tampered).hexdigest().encode() + b'\n')
        run('--open-review', 'tampered.cmr', code=3, reason='payment method, meter or reset rule differs')
        for path in (old, tariff, readings, rates, invoice):
            path.unlink()
        assert run('--open-review', archived) == report
        run('--open-review', archived, '--previous-cycle', old, code=3, reason='archive unavailable')

        inputs(following)
        final = advance(archived, 'may.cmr', june + HOUR)
        close(following[3], final)
        assert run('--open-review', 'may.cmr', '--previous-cycle', archived) == final
        run('--extend-review', archived, '--intervals', readings, '--save-review', 'wrong.cmr',
            code=3, reason='use next-cycle review')

        # Go uses data from the local electricity import bundle format rather than cumulative registers.
        october = utc('2023-09-30T23:00:00')
        november = utc('2023-11-01T00:00:00')
        december = utc('2023-12-01T00:00:00')
        go_old = cycle(october, november, GO_PRODUCT, None, 12, 4, 31, bundle=True)
        go_new = cycle(november, december, GO_PRODUCT, None, 20, 5, 30, bundle=True)
        inputs(go_old)
        close(go_old[3], save('go-october.cmr', november + HOUR))
        inputs(go_new)
        go = advance('go-october.cmr', 'go-november.cmr', december + HOUR)
        close(go_new[3], go)
        assert 'Octopus Go public rate snapshot' in go and 'API quantities are reported' in go
        assert run('--open-review', 'go-november.cmr', '--previous-cycle', 'go-october.cmr') == go
        inputs(cycle(november, december, GO_PRODUCT, Decimal(0), 20, 5, 30))
        refused('go-october.cmr', december + HOUR, 'payment method, meter or reset rule differs')
        print(f'Octopus adjacent cycle review passed {count} installed CLI commands.')


if __name__ == '__main__':
    main(*(Path(path).resolve(strict=True) for path in sys.argv[1:]))
