"""Review signed interim supply charges against the exact annual estimate."""
from hashlib import sha256
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

from test_meter_annual_cycle_cli import declaration, FIXED
from test_meter_bill_named_cli import SCOPE, register, utc


def main(cli):
    months = [utc(f'2024-{month:02d}-01T00:00:00') for month in range(1, 13)]
    begin = months[0]
    end, following = map(utc, ('2025-01-01T00:00:00', '2026-01-01T00:00:00'))
    boundaries = months + [end]
    points = [(t, str(1000 + 60 * min(i, 2))) for i, t in enumerate(boundaries)]
    tariff = declaration(begin, end, following, 1, 1, FIXED)
    tariff['plan']['cycle_charge'] = '0'
    rate = tariff['plan']['revisions'][0]
    rate['base_rate_per_kwh'] = '1'
    rate['bands'][1]['rate_per_kwh'] = '1'
    rows = [{'begin_utc_ms': a, 'end_utc_ms': b, 'supply_charge': '60.00' if i < 2 else '0.00'}
            for i, (a, b) in enumerate(zip(boundaries, boundaries[1:]))]
    year = {'begin_utc_ms': begin, 'end_utc_ms': end,
            'interim_supply_charges': rows, 'true_up_charge': '20.00'}
    statement = {'schema_version': 2, 'scope': SCOPE, 'currency': 'USD',
                 'decimal_places': 2, 'rounding': 'half_even', 'cycles': [year]}

    with TemporaryDirectory(prefix='canary-annual-reconciliation-') as folder:
        root = Path(folder)
        policy, meter, invoice = [root / name for name in
                                  ('private tariff.json', 'private readings.json', 'private \u8d26\u5355.json')]
        source_archive, checked_archive = root / 'meter.cmr', root / 'reconciled.cmr'
        checks = 0

        def write(file, data):
            file.write_bytes(json.dumps(data, separators=(',', ':')).encode('ascii'))

        def run(*args, code=0, reason=None):
            nonlocal checks
            result = subprocess.run([cli, 'meter-bill', *map(str, args)], cwd=root,
                                    capture_output=True, text=True, timeout=15)
            assert result.returncode == code, result
            if code:
                assert not result.stdout and reason in result.stderr, result
            else:
                assert result.stdout and not result.stderr, result
            for text in (str(root), policy.name, meter.name, invoice.name, SCOPE, 'PRIVATE_ACCOUNT_FIXTURE'):
                assert text not in result.stdout + result.stderr, result
            checks += 1
            return result.stdout

        def args(cutoff=end):
            return ('--tariff', policy, '--intervals', meter, '--as-of-utc-ms', cutoff)

        def review(cutoff=end, **kwargs):
            return run(*args(cutoff), '--invoice', invoice, **kwargs)

        def annual_row(**updates):
            return {**statement, 'cycles': [{**year, **updates}]}

        write(policy, tariff)
        write(meter, register(points))
        write(invoice, statement)
        raw = {path: path.read_bytes() for path in (policy, meter, invoice)}
        baseline = run(*args(), '--save-review', source_archive.name)
        assert 'Cycle estimate at closing 140.000000 USD' in baseline
        source_bytes = source_archive.read_bytes()
        reconciled = run(*args(), '--invoice', invoice, '--save-review', checked_archive.name)
        assert 'Annual estimate at invoice precision 140.00 USD' in reconciled
        assert reconciled.count('Interim supply charge [') == 12
        assert 'Interim supply charges total 120.00 USD' in reconciled
        assert 'Estimated annual adjustment 20.00 USD' in reconciled
        assert 'Difference (estimated minus stated adjustment) 0.00 USD (match)' in reconciled
        assert run('--open-review', source_archive, '--invoice', invoice) == reconciled
        assert source_archive.read_bytes() == source_bytes
        assert all(path.read_bytes() == data for path, data in raw.items())

        original = checked_archive.read_bytes()
        assert original.startswith(b'CANARY-METER-REVIEW-3\n')
        assert raw[invoice] in original and b'["annual_invoice",' in original
        for path in raw:
            path.unlink()
        assert run('--open-review', checked_archive) == reconciled
        # Same total, redistributed month amounts, fresh archive checksum. Exact replay must refuse.
        old_charges = b'"supply_charge":"60.00"'
        changed = original[:-65].replace(old_charges, b'"supply_charge":"61.00"', 1)
        changed = changed.replace(old_charges, b'"supply_charge":"59.00"', 1)
        assert changed != original[:-65] and len(changed) == len(original) - 65
        checked_archive.write_bytes(changed + sha256(changed).hexdigest().encode() + b'\n')
        run('--open-review', checked_archive, code=3, reason='differ from the recalculated amounts')
        checked_archive.write_bytes(original)
        for path, data in raw.items():
            path.write_bytes(data)

        write(invoice, annual_row(true_up_charge=None))
        missing = review()
        assert 'Estimated annual adjustment 20.00 USD' in missing and 'Stated annual adjustment' not in missing
        write(invoice, annual_row(true_up_charge='19.99'))
        assert 'Difference (estimated minus stated adjustment) 0.01 USD (mismatch)' in review()
        more = [dict(row) for row in rows]
        more[0]['supply_charge'] = '90.00'
        write(invoice, annual_row(interim_supply_charges=more, true_up_charge='-10.00'))
        credit = review()
        assert 'Estimated annual adjustment -10.00 USD' in credit and '(match)' in credit
        more[2]['supply_charge'] = '-1.25'
        write(invoice, annual_row(interim_supply_charges=more, true_up_charge='-8.75'))
        assert 'Estimated annual adjustment -8.75 USD' in review()

        # Rounding the annual amount first differs from rounding the difference at a half-even tie.
        tariff['plan']['cycle_charge'] = '0.005'
        write(policy, tariff)
        lower = [dict(row) for row in rows]
        lower[0]['supply_charge'] = '59.99'
        tie = annual_row(interim_supply_charges=lower, true_up_charge=None)
        write(invoice, tie)
        assert 'Estimated annual adjustment 20.01 USD' in review()
        write(invoice, {**tie, 'rounding': 'half_away_from_zero'})
        assert 'Estimated annual adjustment 20.02 USD' in review()
        policy.write_bytes(raw[policy])

        for bad, reason in [
            ({**statement, 'scope': 'different'}, 'scope or currency differs'),
            ({**statement, 'currency': 'GBP'}, 'scope or currency differs'),
            ({**statement, 'account': 'PRIVATE_ACCOUNT_FIXTURE'}, 'invalid invoice'),
            (annual_row(true_up_charge='0.001'), 'invalid stated annual adjustment'),
            (annual_row(true_up_charge=20), 'invalid stated annual adjustment'),
            (annual_row(interim_supply_charges=[]), 'invalid annual invoice'),
            (annual_row(interim_supply_charges=rows[:-1]), 'without gaps or overlap'),
            (annual_row(interim_supply_charges=[{**rows[0], 'begin_utc_ms': begin + 1}, *rows[1:]]),
             'without gaps or overlap'),
            (annual_row(interim_supply_charges=[{**rows[0], 'end_utc_ms': rows[0]['end_utc_ms'] + 1}, *rows[1:]]),
             'without gaps or overlap'),
            (annual_row(interim_supply_charges=[{**rows[0], 'supply_charge': '1e2'}, *rows[1:]]),
             'invalid interim invoice'),
            (annual_row(begin_utc_ms=begin + 1, interim_supply_charges=[
                {**rows[0], 'begin_utc_ms': begin + 1}, *rows[1:]]), 'bounds differ'),
        ]:
            write(invoice, bad)
            run(*args(), '--invoice', invoice, '--save-review', 'rejected.cmr', code=3, reason=reason)
            assert not (root / 'rejected.cmr').exists() and not (root / 'rejected.cmr.tmp').exists()
        write(invoice, {**statement, 'cycles': [{k: v for k, v in year.items() if k != 'true_up_charge'}]})
        review(code=3, reason='invalid annual invoice')
        invoice.write_bytes(raw[invoice])

        write(policy, {**tariff, 'schema_version': 1, 'reset_policy': 'explicit_cycle_only'})
        review(code=3, reason='requires an annual tariff reset')
        policy.write_bytes(raw[policy])
        write(meter, register(points[:-1]))
        review(months[-1], code=3, reason='requires complete billing cycles')
        later_opening = {**tariff, 'opening': {**tariff['opening'], 'through_utc_ms': months[1],
                                               'imported_kwh': '60', 'accrued_subtotal': '60'}}
        write(policy, later_opening)
        write(meter, register(points[1:]))
        review(code=3, reason='from a zero opening')

        # A continuation first audits the previous adjustment but carries only its meter prefix forward.
        beyond = end + 3600000
        write(meter, register(points + [(beyond, '1121')]))
        extension = run('--extend-review', checked_archive, '--intervals', meter,
                        '--as-of-utc-ms', beyond, '--save-review', 'extension.cmr')
        assert 'Cycle estimate at closing 140.000000 USD' in extension
        assert 'Provisional cycle estimate 1.000000 USD' in extension
        assert 'Estimated annual adjustment' not in extension
        assert checked_archive.read_bytes() == original
        assert run('--open-review', 'extension.cmr') == extension

        # Both complete annual periods can retain fresh statement evidence in the v4 continuation.
        second_year = {'begin_utc_ms': end, 'end_utc_ms': following, 'true_up_charge': '1.00',
                       'interim_supply_charges': [
                           {'begin_utc_ms': end, 'end_utc_ms': following, 'supply_charge': '0.00'}]}
        write(invoice, {**statement, 'cycles': [year, second_year]})
        write(meter, register(points + [(following, '1121')]))
        combined = run('--extend-review', checked_archive, '--intervals', meter, '--invoice', invoice,
                       '--as-of-utc-ms', following, '--save-review', 'two-years.cmr')
        assert combined.count('Difference (estimated minus stated adjustment) 0.00 USD (match)') == 2
        assert (root / 'two-years.cmr').read_bytes().startswith(b'CANARY-METER-REVIEW-4\n')
        assert checked_archive.read_bytes() == original
        for path in (meter, invoice, checked_archive):
            path.unlink()
        assert run('--open-review', 'two-years.cmr') == combined

    print(f'Annual supply reconciliation: {checks} native CLI cases')


if __name__ == '__main__':
    main(Path(sys.argv[1]).resolve(strict=True))
