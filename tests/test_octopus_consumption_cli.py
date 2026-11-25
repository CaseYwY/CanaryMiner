"""Review documented Octopus electricity export pages through the installed CLI."""
import copy
import datetime as dt
from decimal import Decimal, ROUND_HALF_EVEN
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from test_meter_bill_named_cli import BUNDLE, SCOPE, named, plan, utc
from test_octopus_meter_rates_cli import BEGIN, END, HALF_HOUR, SLOTS, inputs


ROOT = Path(__file__).resolve().parents[1]
PRIVATE = 'fixture-private-meter-and-key'


def as_utc(instant):
    return dt.datetime.fromtimestamp(instant // 1000, dt.timezone.utc)


def utc_text(instant):
    return as_utc(instant).isoformat().replace('+00:00', 'Z')


def supplier_rows(register_points, transition, summer_after):
    def local(instant):
        summer = (instant >= transition) == summer_after
        if summer:
            return as_utc(instant).astimezone(dt.timezone(dt.timedelta(hours=1))).isoformat()
        return utc_text(instant)

    return [{'consumption': float(Decimal(right['imported_kwh']) - Decimal(left['imported_kwh'])),
             'interval_start': local(left['utc_ms']), 'interval_end': local(right['utc_ms'])}
            for left, right in zip(register_points, register_points[1:])]


def main(cli, bundle):
    with tempfile.TemporaryDirectory(prefix='canary-octopus-export-') as tmp:
        root = Path(tmp)
        installed = root / 'installed'
        installed.mkdir()
        binary = Path(shutil.copy2(cli, installed / cli.name))
        shutil.copytree(bundle, installed / BUNDLE)
        tariff, rate_file, consumption = [root / name for name in ('tariff.json', 'rates.json', 'import.json')]
        archive = root / 'import.cmr'

        def write(path, data):
            path.write_text(json.dumps(data, separators=(',', ':')), encoding='utf-8')

        def pages(records):
            paths = []
            link = f'https://api.oegb-kraken.energy/v1/electricity-meter-points/0000000000000/meters/{PRIVATE}/consumption/'
            for i in range(0, len(records), 100):
                path = root / f'{PRIVATE}-{i}.json'
                write(path, {'count': len(records), 'next': link if i + 100 < len(records) else None,
                             'previous': link if i else None, 'results': records[i:i + 100]})
                paths.append(path)
            return paths

        def command(*argv):
            result = subprocess.run(argv, cwd=root, capture_output=True, text=True, timeout=12)
            assert PRIVATE not in result.stdout + result.stderr, result
            return result

        def prepare(paths, begin, end, flag='--confirm-electricity-import', scope=SCOPE):
            confirm = [flag] if flag else []
            return command(sys.executable, '-B', ROOT / 'tools/prepare_octopus_consumption.py',
                           *confirm, '--scope', scope, '--begin', utc_text(begin),
                           '--end', utc_text(end), '--output', consumption, *paths)

        def review(*args):
            return command(binary, 'meter-bill', '--tariff', tariff, '--intervals', consumption, *map(str, args))

        def refuse(result, phrase):
            assert result.returncode == 3 and not result.stdout and phrase in result.stderr, result

        decl, rates, register, physical, billed, total = inputs()
        source = supplier_rows(register['register_readings'], utc('2023-03-26T01:00:00'), True)
        assert any(r['interval_start'].endswith('00:30:00Z') and r['interval_end'].endswith('02:00:00+01:00')
                   for r in source)
        paths = pages(list(reversed(source)))
        assert len(source) == SLOTS == 1486 and len(paths) == 15
        old_bytes = [p.read_bytes() for p in paths]
        assert prepare(paths, BEGIN, END, flag=None).returncode == 2 and not consumption.exists()
        assert prepare(paths, BEGIN, END, flag='--confirm').returncode == 2 and not consumption.exists()
        assert prepare(paths, BEGIN, END, scope=' ' + SCOPE).returncode == 2 and not consumption.exists()
        assert prepare(paths, BEGIN, END, scope='\u7535\u8868').returncode == 2 and not consumption.exists()
        assert prepare(paths[:-1], BEGIN, END).returncode == 1 and not consumption.exists()
        assert prepare([paths[1], paths[0], *paths[2:]], BEGIN, END).returncode == 1 and not consumption.exists()
        different_meter = json.loads(old_bytes[1])
        different_meter['previous'] = different_meter['previous'].replace('0000000000000', '0000000000001')
        write(paths[1], different_meter)
        assert prepare(paths, BEGIN, END).returncode == 1 and not consumption.exists()
        paths[1].write_bytes(old_bytes[1])
        result = prepare(paths, BEGIN, END)
        assert result.returncode == 0 and not result.stderr, result
        assert [p.read_bytes() for p in paths] == old_bytes
        bundled = consumption.read_bytes()
        assert PRIVATE.encode() not in bundled and b'https://' not in bundled
        assert prepare(paths, BEGIN, END).returncode == 2 and consumption.read_bytes() == bundled
        observation = json.loads(bundled)['observed_utc_ms']
        for path in paths:
            path.unlink()

        write(tariff, decl)
        write(rate_file, rates)
        options = ('--octopus-rates', rate_file, '--as-of-utc-ms', observation)
        saved = review(*options, '--save-review', archive.name)
        assert saved.returncode == 0 and not saved.stderr, saved
        for fragment in (f'Local bundle observation {observation}', '0.001 kWh',
                         f'Imported usage {physical:.6f} kWh', f'Billed usage {billed:.6f} kWh',
                         f'Cycle estimate at closing {total:.6f} GBP'):
            assert fragment in saved.stdout, saved
        assert PRIVATE.encode() not in archive.read_bytes()
        refuse(review('--octopus-rates', rate_file, '--as-of-utc-ms', observation - 1), 'observed after the review cutoff')

        original = json.loads(bundled)
        for mutation, expected in (
            (lambda s: s['consumption'][0].update(consumption=-0.001), 'invalid Octopus half-hour'),
            (lambda s: s['consumption'][0].update(consumption=0.0001), 'invalid Octopus half-hour'),
            (lambda s: s['consumption'][0].update(interval_start='2023-03-01T00:00:00-14:01'), 'invalid Octopus half-hour'),
            (lambda s: s['consumption'].pop(), 'without gaps or overlap'),
            (lambda s: s['consumption'].__setitem__(1, s['consumption'][0]), 'without gaps or overlap'),
            (lambda s: s.update(scope='different-meter'), 'meter scope differs'),
        ):
            malformed = copy.deepcopy(original)
            mutation(malformed)
            write(consumption, malformed)
            refuse(review(*options), expected)
        for path in (tariff, consumption, rate_file):
            path.unlink()
        restored = command(binary, 'meter-bill', '--open-review', archive)
        assert restored.returncode == 0 and restored.stdout == saved.stdout and not restored.stderr, restored

        # The two occurrences of 01:00 on the fall transition are separate elapsed half hours.
        fall_begin, fall_end = utc('2023-10-28T23:00:00'), utc('2023-10-30T00:00:00')
        fall_plan = plan(fall_begin, fall_begin, fall_end, named('Europe/London'))
        fall_plan['plan'].update(supplier='Octopus Energy', currency='GBP',
                                 rounding='meter_import_half_hour_kwh_half_even_2')
        write(tariff, fall_plan)
        step = Decimal('0.025')
        rows = [{'utc_ms': fall_begin + i * HALF_HOUR, 'imported_kwh': str(i * step)} for i in range(51)]
        repeat = supplier_rows(rows, utc('2023-10-29T01:00:00'), False)
        assert {'2023-10-29T01:00:00+01:00', '2023-10-29T01:00:00Z'} <= {
            row['interval_start'] for row in repeat}
        result = prepare(pages(repeat), fall_begin, fall_end)
        assert result.returncode == 0, result
        invoice = review()
        fall_billed = 50 * step.quantize(Decimal('0.01'), rounding=ROUND_HALF_EVEN)
        assert invoice.returncode == 0 and f'Cycle estimate at closing {fall_billed:.6f} GBP' in invoice.stdout, invoice
    print('Octopus export import: 15 pages, 1,486 spring half hours, 50 fall half hours, privacy, exact bills and replay')


if __name__ == '__main__':
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
