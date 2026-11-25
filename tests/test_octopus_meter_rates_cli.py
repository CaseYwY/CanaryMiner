"""Review a complete London civil month of paginated Agile prices through the native CLI."""
import copy
import datetime as dt
from decimal import Decimal, ROUND_HALF_EVEN
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from test_meter_bill_named_cli import BUNDLE, HOUR, SCOPE, named, plan, register, utc


PRODUCT = 'AGILE-FLEX-22-11-25'
PAYMENT = 'DIRECT_DEBIT'
HALF_HOUR = HOUR // 2
BEGIN = utc('2023-03-01T00:00:00')
FEE_CHANGE = utc('2023-03-26T23:00:00')
END = utc('2023-03-31T23:00:00')
OBSERVED = END + HOUR
SLOTS = (END - BEGIN) // HALF_HOUR


def decimal_text(amount):
    return format(amount.normalize(), 'f')


def timestamp(instant):
    return dt.datetime.fromtimestamp(instant // 1000, dt.timezone.utc).isoformat().replace('+00:00', 'Z')


def row(begin, end, pence, payment=None):
    return {'valid_from': timestamp(begin), 'valid_to': timestamp(end) if end is not None else None,
            'value_inc_vat': pence, 'value_exc_vat': pence, 'payment_method': payment}


def pages(rows, size=100):
    return [
        {'count': len(rows), 'previous': 'previous' if i else None,
         'next': 'next' if i + size < len(rows) else None, 'results': rows[i:i + size]}
        for i in range(0, len(rows), size)
    ]


def inputs():
    document = plan(BEGIN, BEGIN, END, named('Europe/London'))
    document.update(confirmed_utc=BEGIN // 1000)
    document['plan'].update(territory='C', supplier='Octopus Energy', product=PRODUCT, currency='GBP',
                            rounding='meter_import_half_hour_kwh_half_even_2')
    document['plan']['revisions'][0].update(time_mode='dynamic', base_rate_per_kwh='0')
    prices = [i % 33 - 12 for i in range(SLOTS)]
    rows = [row(BEGIN + i * HALF_HOUR, BEGIN + (i + 1) * HALF_HOUR, price)
            for i, price in enumerate(prices)]
    rates = {
        'schema_version': 1, 'product_code': PRODUCT, 'tariff_code': f'E-1R-{PRODUCT}-C',
        'payment_method': PAYMENT, 'observed_utc_ms': OBSERVED,
        'unit_rate_pages': pages(list(reversed(rows))),
        'standing_charge_pages': pages([
            row(FEE_CHANGE, None, 43.125, PAYMENT),
            row(BEGIN - HOUR, None, 50, 'NON_DIRECT_DEBIT'),
            row(BEGIN - HOUR, FEE_CHANGE, 31.51, PAYMENT),
        ]),
    }
    physical = Decimal(0)
    billed = Decimal(0)
    usage_charge = Decimal(0)
    points = [(BEGIN, '20')]
    for i, price in enumerate(prices):
        delta = Decimal(('0.015', '0.025', '0.034', '0.005', '0')[i % 5])
        rounded = delta.quantize(Decimal('0.01'), rounding=ROUND_HALF_EVEN)
        physical += delta
        billed += rounded
        usage_charge += rounded * Decimal(price) / 100
        points.append((BEGIN + (i + 1) * HALF_HOUR, decimal_text(20 + physical)))
    standing = (26 * Decimal('31.51') + 5 * Decimal('43.125')) / 100
    return document, rates, register(points), physical, billed, usage_charge + standing


def main(cli, bundle):
    assert SLOTS == 1486
    declaration, snapshot, readings, physical, billed, total = inputs()
    with tempfile.TemporaryDirectory(prefix='canary-agile-rates-') as tmp:
        root = Path(tmp)
        installed = root / 'installed'
        installed.mkdir()
        binary = Path(shutil.copy2(cli, installed / cli.name))
        shutil.copytree(bundle, installed / BUNDLE)
        tariff, meter, rate_file = [root / name for name in ('private tariff.json', 'private meter.json', 'private \u7535\u4ef7.json')]
        archive = root / 'march.cmr'

        def write(path, data):
            path.write_text(json.dumps(data, separators=(',', ':')), encoding='utf-8')

        def run(*args):
            result = subprocess.run([binary, 'meter-bill', *args], cwd=root, capture_output=True, text=True, timeout=15)
            for private in (tariff.name, meter.name, rate_file.name, SCOPE, str(installed)):
                assert private not in result.stdout + result.stderr, result
            return result

        def review(*args, cutoff=OBSERVED):
            return run('--tariff', tariff.name, '--intervals', meter.name,
                       '--octopus-rates', rate_file.name, '--as-of-utc-ms', str(cutoff), *args)

        def reject(result, reason):
            assert result.returncode == 3 and not result.stdout and reason in result.stderr, result

        write(tariff, declaration)
        write(meter, readings)
        write(rate_file, snapshot)
        encoded = rate_file.read_bytes()
        scientific = encoded.replace(b'"value_inc_vat":43.125', b'"value_inc_vat":4.3125E+1')
        scientific = scientific.replace(b'"value_exc_vat":31.51', b'"value_exc_vat":3151e-2')
        scientific = scientific.replace(b'"value_inc_vat":-12,', b'"value_inc_vat":-0.12e2,')
        assert scientific != encoded
        rate_file.write_bytes(scientific.ljust(2 * 1024 * 1024, b' '))
        original_inputs = [path.read_bytes() for path in (tariff, meter, rate_file)]
        result = review('--save-review', archive.name)
        assert result.returncode == 0 and not result.stderr, result
        for fragment in (f'{SLOTS} half-hour prices', f'Observed {OBSERVED} UTC ms', PAYMENT,
                         f'Imported usage {physical:.6f} kWh', f'Billed usage {billed:.6f} kWh',
                         f'Cycle estimate at closing {total:.6f} GBP'):
            assert fragment in result.stdout, (fragment, result)
        before = archive.read_bytes()
        assert len(before) > 2 * 1024 * 1024 and before.startswith(b'CANARY-METER-REVIEW-2\n')
        assert [path.read_bytes() for path in (tariff, meter, rate_file)] == original_inputs
        for path in (tariff, meter, rate_file):
            path.unlink()
        reopened = run('--open-review', archive.name)
        assert reopened.returncode == 0 and not reopened.stderr and reopened.stdout == result.stdout, reopened
        assert archive.read_bytes() == before

        # A recomputed checksum cannot hide a price change from exact replay.
        rehashed = before[:-65].replace(b'"value_inc_vat":4.3125E+1', b'"value_inc_vat":4.9125E+1')
        assert rehashed != before[:-65]
        archive.write_bytes(rehashed + hashlib.sha256(rehashed).hexdigest().encode('ascii') + b'\n')
        reject(run('--open-review', archive.name), 'saved review amounts differ')

        write(tariff, declaration)
        write(meter, readings)
        write(rate_file, snapshot)
        reject(review(cutoff=OBSERVED - 1), 'observed after the review cutoff')

        rate_file.write_bytes(encoded.replace(b'"value_inc_vat":43.125', b'"value_inc_vat":4e-19'))
        reject(review(), 'invalid or incomplete Octopus Agile rate pages')
        rate_file.write_bytes(encoded.ljust(2 * 1024 * 1024 + 1, b' '))
        reject(review(), 'rate file unavailable, empty, oversized')
        write(rate_file, {**snapshot, 'extra': True})
        reject(review(), 'invalid Octopus Agile rate file')

        identity = copy.deepcopy(snapshot)
        identity['tariff_code'] = f'E-1R-{PRODUCT}-D'
        write(rate_file, identity)
        reject(review('--save-review', 'refused.cmr'), 'invalid Octopus Agile rate identity')
        assert not (root / 'refused.cmr').exists()

        incomplete = copy.deepcopy(snapshot)
        incomplete['unit_rate_pages'].pop()
        write(rate_file, incomplete)
        reject(review(), 'invalid or incomplete Octopus Agile rate pages')

        overlapping = copy.deepcopy(snapshot)
        overlapping['unit_rate_pages'][0]['results'][0] = overlapping['unit_rate_pages'][0]['results'][1]
        write(rate_file, overlapping)
        reject(review(), 'cover every exact half hour once')

        midday = copy.deepcopy(snapshot)
        fees = midday['standing_charge_pages'][0]['results']
        fees[0]['valid_from'] = fees[2]['valid_to'] = timestamp(FEE_CHANGE + HALF_HOUR)
        write(rate_file, midday)
        reject(review(), 'complete, unambiguous London days')

        ambiguous = copy.deepcopy(snapshot)
        ambiguous['standing_charge_pages'][0]['results'][1]['payment_method'] = None
        write(rate_file, ambiguous)
        reject(review(), 'complete, unambiguous London days')

        write(rate_file, snapshot)
        manual = copy.deepcopy(declaration)
        manual['plan']['revisions'][0]['charge_per_civil_day'] = '0.1'
        write(tariff, manual)
        reject(review(), 'tariff declaration is unsuitable')

        write(tariff, declaration)
        rate_file.write_bytes(b'')
        reject(review(), 'rate file unavailable, empty')
    print(f'Octopus Agile CLI: {SLOTS} half hours, 31 London days, exact signed prices, standing changes, and archive replay')


if __name__ == '__main__':
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
