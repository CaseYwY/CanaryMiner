"""Reconcile complete native bills with declared electricity supply charges."""
import copy
from hashlib import sha256
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
BEGIN, APRIL, END = 1709251200000, 1711929600000, 1714521600000
HOUR = 3600000
SCOPE = 'fixture-private-invoice-scope'
HEADER = b'CANARY-METER-REVIEW-3\n'


def main(cli):
    with tempfile.TemporaryDirectory(prefix='canary-invoice-review-') as directory:
        root = Path(directory)
        tariff = root / 'private tariff.json'
        meter = root / 'private readings.json'
        invoice = root / 'private \u8d26\u5355.json'
        ordinary = root / 'without-invoice.cmr'
        archive = root / 'with-invoice.cmr'

        def write(path, value):
            path.write_bytes(json.dumps(value, separators=(',', ':')).encode('utf-8') + b'\n')

        def run(*args):
            result = subprocess.run([cli, 'meter-bill', *map(str, args)], cwd=root,
                                    capture_output=True, text=True, timeout=10)
            for private in (tariff.name, meter.name, invoice.name, SCOPE, 'FORBIDDEN_DEVICE_SETUP'):
                assert private not in result.stdout + result.stderr, result
            return result

        def refuse(result, phrase, code=3):
            assert result.returncode == code and not result.stdout and phrase in result.stderr, result

        def accept(result):
            assert result.returncode == 0 and not result.stderr, result
            return result.stdout

        plan = json.loads((ROOT / 'config/tariff-declaration.example.json').read_bytes())
        plan.update(schema_version=2, confirmed_utc=BEGIN // 1000,
                    reset_policy={'monthly_day': 1, 'clock': {'kind': 'utc', 'fixed_offset_seconds': 0}})
        plan['plan'].update(tiers='none', cycle_charge='0.005', subtotal_multiplier='1')
        plan['plan']['revisions'][0].update(
            begin_utc_ms=BEGIN, end_utc_ms=END, published_utc_ms=BEGIN, time_mode='constant',
            base_rate_per_kwh='0', charge_per_civil_day='0', calendar=[], dynamic=[], bands=[])
        plan['opening'].update(scope=SCOPE, cycle_begin_utc_ms=BEGIN, cycle_end_utc_ms=APRIL, through_utc_ms=BEGIN)
        readings = {'schema_version': 1, 'scope': SCOPE, 'intervals': [
            {'begin_utc_ms': BEGIN, 'end_utc_ms': END, 'average_watts': '1000'}
        ]}
        declared = {'schema_version': 1, 'scope': SCOPE, 'currency': 'USD', 'decimal_places': 2,
                    'rounding': 'half_even', 'cycles': [
                        {'begin_utc_ms': BEGIN, 'end_utc_ms': APRIL, 'supply_charge': '0.00'},
                        {'begin_utc_ms': APRIL, 'end_utc_ms': END, 'supply_charge': '0.03'},
                    ]}
        write(tariff, plan)
        write(meter, readings)
        write(invoice, declared)
        originals = {path: path.read_bytes() for path in (tariff, meter, invoice)}
        args = ('--tariff', tariff.name, '--intervals', meter.name, '--as-of-utc-ms', END)
        compare = (*args, '--invoice', invoice.name)
        baseline = accept(run(*args, '--save-review', ordinary.name))
        assert 'Invoice' not in baseline
        previous = ordinary.read_bytes()
        attached = accept(run('--invoice', invoice.name, '--open-review', ordinary.name))
        assert attached == accept(run(*compare))
        assert attached.count('Calculated at invoice precision 0.00 USD') == 2
        assert 'Difference (calculated minus invoice) 0.00 USD (match)' in attached
        assert 'Difference (calculated minus invoice) -0.03 USD (mismatch)' in attached
        assert ordinary.read_bytes() == previous and previous.startswith(b'CANARY-METER-REVIEW-1\n')
        assert accept(run(*compare, '--save-review', archive.name)) == attached
        assert {path: path.read_bytes() for path in originals} == originals
        refuse(run('--open-review', archive.name, '--invoice', invoice.name), 'already contains an invoice')
        refuse(run('--open-review', archive.name, '--invoice', invoice.name, '--save-review', 'copy.cmr'),
               'invalid options', 2)
        refuse(run(*compare, '--invoice', invoice.name), 'invalid options', 2)
        result = run(*args, '--invoic\uff45', invoice.name)
        assert result.returncode in (2, 3) and not result.stdout

        before = archive.read_bytes()
        assert before.startswith(HEADER)
        cutoff, tail = before[len(HEADER):].split(b'\n', 1)
        parts = []
        for _ in range(4):
            length, tail = tail.split(b'\n', 1)
            parts.append(tail[:int(length)])
            tail = tail[int(length):]
        assert int(cutoff) == END and tail == sha256(before[:-65]).hexdigest().encode() + b'\n'
        assert [parts[0], parts[1], parts[3]] == list(originals.values())
        if os.name != 'nt':
            assert archive.stat().st_mode & 0o077 == 0
        for path in originals:
            path.unlink()
        assert accept(run('--open-review', archive.name)) == attached
        assert archive.read_bytes() == before

        # A valid checksum cannot make changed comparison amounts replay as the original.
        altered = before[:-65].replace(b'"supply_charge":"0.03"', b'"supply_charge":"0.04"')
        assert altered != before[:-65]
        archive.write_bytes(altered + sha256(altered).hexdigest().encode() + b'\n')
        refuse(run('--open-review', archive.name), 'differ from the recalculated amounts')
        archive.write_bytes(before)
        for path, data in originals.items():
            path.write_bytes(data)

        away = {**declared, 'rounding': 'half_away_from_zero'}
        write(invoice, away)
        out = accept(run(*compare))
        assert out.count('Calculated at invoice precision 0.01 USD') == 2
        assert 'Difference (calculated minus invoice) 0.01 USD (mismatch)' in out
        assert 'Difference (calculated minus invoice) -0.02 USD (mismatch)' in out

        for change, phrase in (
            ({'currency': 'GBP'}, 'scope or currency differs'),
            ({'scope': 'other'}, 'scope or currency differs'),
            ({'account_name': 'private'}, 'invalid invoice comparison document'),
            ({'rounding': 'floor'}, 'invalid invoice comparison document'),
            ({'decimal_places': 1}, 'invalid invoice cycle or supply charge'),
            ({'cycles': declared['cycles'][:1]}, 'one charge for each reviewed cycle'),
            ({'cycles': [{**declared['cycles'][0], 'begin_utc_ms': BEGIN + HOUR}, declared['cycles'][1]]},
             'invoice cycle bounds differ'),
            ({'cycles': list(reversed(declared['cycles']))}, 'invalid invoice cycle or supply charge'),
        ):
            write(invoice, {**declared, **change})
            refuse(run(*compare, '--save-review', 'rejected.cmr'), phrase)
            assert not (root / 'rejected.cmr').exists() and not (root / 'rejected.cmr.tmp').exists()
        write(invoice, declared)
        partial = copy.deepcopy(readings)
        partial['intervals'][0]['end_utc_ms'] = END - HOUR
        write(meter, partial)
        refuse(run(*compare), 'requires complete billing cycles')
        partial = copy.deepcopy(readings)
        partial['intervals'][0]['begin_utc_ms'] = BEGIN + HOUR
        write(meter, partial)
        plan['opening']['through_utc_ms'] = BEGIN + HOUR
        write(tariff, plan)
        refuse(run(*compare), 'from a zero opening')

        invoice.write_bytes(b' ' * (16 * 1024 + 1))
        refuse(run(*compare), 'invoice file unavailable, empty, oversized')
        if os.name != 'nt':
            fifo = root / 'fifo.json'
            os.mkfifo(fifo)
            refuse(run(*args, '--invoice', fifo.name), 'invoice file unavailable')

        signed = copy.deepcopy(plan)
        signed.update(schema_version=1, reset_policy='explicit_cycle_only')
        signed['plan']['cycle_charge'] = '0'
        signed['plan']['revisions'][0].update(end_utc_ms=BEGIN + HOUR, base_rate_per_kwh='-0.005')
        signed['opening'].update(cycle_end_utc_ms=BEGIN + HOUR, through_utc_ms=BEGIN)
        signed_meter = copy.deepcopy(readings)
        signed_meter['intervals'][0]['end_utc_ms'] = BEGIN + HOUR
        signed_charge = {**declared, 'cycles': [
            {'begin_utc_ms': BEGIN, 'end_utc_ms': BEGIN + HOUR, 'supply_charge': '-0.01'}
        ]}
        write(tariff, signed)
        write(meter, signed_meter)
        write(invoice, signed_charge)
        assert 'Difference (calculated minus invoice) 0.01 USD (mismatch)' in accept(run(*compare))
        write(invoice, {**signed_charge, 'rounding': 'half_away_from_zero'})
        assert 'Calculated at invoice precision -0.01 USD' in accept(run(*compare))
        write(invoice, signed_charge)
        signed['plan']['revisions'][0]['base_rate_per_kwh'] = '-0.0050001'
        write(tariff, signed)
        exact = accept(run(*compare))
        assert 'Cycle estimate at closing -0.005000 USD' in exact
        assert 'Calculated at invoice precision -0.01 USD' in exact
        assert 'Difference (calculated minus invoice) 0.00 USD (match)' in exact
    print('Native invoice review: exact ties, complete cycles, signed differences and private offline replay')


if __name__ == '__main__':
    main(Path(sys.argv[1]).resolve())
