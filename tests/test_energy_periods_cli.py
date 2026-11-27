"""The installed energy CLI reads one audited source and selects civil periods."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

from test_meter_bill_named_cli import BUNDLE, HOUR, named, plan, utc
from test_octopus_meter_rates_cli import GO_PRODUCT, HALF_HOUR, PAYMENT, PRODUCT, pages, row


def main(host, ordinary, auth, collector, fixture, tzdb=None):
    with TemporaryDirectory(prefix='canary-energy-periods-') as folder:
        root = Path(folder)
        installed = root / 'installed bin \u03a9'
        installed.mkdir()
        for binary in (host, auth, collector):
            shutil.copy2(binary, installed / binary.name)
        host = installed / host.name
        if tzdb:
            shutil.copytree(tzdb, installed / BUNDLE)
        state = root / 'state'
        file = root / 'reset rule \u8d26.json'
        begin = utc('2024-03-01T00:00:00')
        boundary = utc('2024-03-31T23:00:00') if tzdb else utc('2024-04-01T00:00:00')
        end = utc('2024-04-30T23:00:00') if tzdb else utc('2024-05-01T00:00:00')
        through = boundary + 2000
        clock = named('Europe/London') if tzdb else {'kind': 'utc', 'fixed_offset_seconds': 0}
        counts = 0

        def policy(day=1, clock=clock):
            file.write_text(json.dumps({'monthly_day': day, 'clock': clock}), encoding='utf-8')

        def run(*args, binary=host, code=0, message=None):
            nonlocal counts
            result = subprocess.run([binary, 'energy-periods', *map(str, args)], cwd=root,
                                    capture_output=True, text=True, timeout=25)
            assert result.returncode == code, result
            if code:
                assert not result.stdout and result.stderr and (message is None or message in result.stderr), result
            else:
                assert result.stdout and not result.stderr, result
            assert all(private not in result.stdout + result.stderr for private in
                       (str(root), file.name, 'fixture.local-session', 'channel.', 'selection.', 'energy-persistence', 'private-rate-label', 'rate notes')), result
            counts += 1
            return result.stdout

        def report(directory=state, at=through, cycles=2, rates=(), extra=(), **kwargs):
            rate_args = [item for rate in rates for item in ('--flat-rate', rate)]
            return run('--state-dir', directory, '--through-utc-ms', at,
                       '--reset-policy', file, '--cycles', cycles, *rate_args, *extra, **kwargs)

        def rate_file(name, begin_ms, end_ms, amount, *, currency='USD', confirmed=None, enabled=True):
            path = root / (name + '.json')
            data = dict(schema_version=1, rule_model='flat_energy_only', measurement_basis='gpu_board_proxy',
                        collect_energy=False, show_energy=False, tariff_enabled=enabled, exclusions_acknowledged=True,
                        record_interval_ms=1000, maximum_gap_ms=5000, country='XX', region='local',
                        supplier='private-rate-label', product='example', version=f'example-{begin_ms}-{end_ms}',
                        currency=currency, rate_per_kwh=amount,
                        clock='utc', fixed_offset_seconds=0, effective_from_utc=begin_ms // 1000,
                        effective_through_utc=end_ms // 1000,
                        confirmed_utc=(begin_ms if confirmed is None else confirmed) // 1000)
            path.write_text(json.dumps(data), encoding='ascii')
            return path

        def seed(directory, shape, at=boundary):
            subprocess.run([fixture, directory, str(at), shape], cwd=root, check=True, timeout=20)
            return next(directory.glob('energy-history.*.db'))

        def snapshot(directory):
            return {p.name: p.read_bytes() for p in directory.iterdir()
                    if p.name.endswith(('.json', '.db'))}

        help_text = run('--help', binary=ordinary)
        assert all(flag in help_text for flag in ('--through-utc-ms', '--reset-policy', '--flat-rate', '--octopus-rates'))
        policy()
        report(binary=ordinary, code=3, message='unavailable in this build')
        assert not state.exists()
        report(code=3)
        assert not state.exists()
        for flags in (('--cycles', 0), ('--cycles', 13), ('--cycles', '02'),
                      ('--through-utc-ms', 253402300800000), ('--config', file), ('--help',),
                      ('--state-dir', state), ('--reset-policy', file)):
            run('--state-dir', state, '--reset-policy', file, *flags, code=2, message='invalid options')
        report(at=253402300799999, code=3, message='past or present')
        run('--state-dir', state, '--reset-policy', file, '--flat-rate', code=2, message='invalid options')
        report(rates=(file,) * 25, code=2, message='invalid options')
        for invalid in ('{"monthly_day":1,"monthly_day":2,"clock":{}}', '{"monthly_day":31}',
                        '{"monthly_day":0,"clock":{}}', '{"monthly_day":1,"clock":{},"unknown":false}'):
            file.write_text(invalid, encoding='ascii')
            report(code=3)
        file.write_text('[]', encoding='ascii')
        report(code=3, message='energy-periods: reset policy unavailable or invalid')
        policy()
        empty = root / 'empty'
        seed(empty, 'empty')
        result = report(empty)
        assert 'Audited sequence 0' in result and result.count(': unavailable') == 2 and 'covered energy' not in result

        for shape in ('v1', 'v2'):
            destination = root / shape
            database = seed(destination, shape)
            original = snapshot(destination)
            result = report(destination)
            first = f'Period [{begin}, {boundary}) through {boundary}: partial (4000/'
            second = (f'Period [{boundary}, {end}) through {through}: complete (4000/4000 GPU-ms), '
                      'covered energy 0.000166667 kWh (exact 600000 W-ms)')
            assert first in result and result.count('exact 600000 W-ms') == 2 and second in result, result
            assert 'Audited sequence 2' in result and 'selected boards 2' in result
            result = report(destination, at=boundary)
            assert f'Period [{boundary}, {end}) through {boundary}: no elapsed time' in result
            assert result.count('covered energy') == 1
            result = report(destination, at=boundary + 5000, cycles=12)
            assert result.count('Period [') == 12 and result.count(': unavailable') == 10
            assert 'partial (8000/10000 GPU-ms)' in result and result.count('exact 600000 W-ms') == 2
            assert snapshot(destination) == original

            journal = Path(str(database) + '.energy-recovery.json')
            moved = journal.with_suffix('.saved')
            journal.rename(moved)
            report(destination, code=3, message='verified local energy history unavailable')
            assert not journal.exists() and moved.read_bytes() == original[journal.name]
            moved.rename(journal)
            journal.write_text('corrupt', encoding='ascii')
            report(destination, code=3)
            assert journal.read_bytes() == b'corrupt'
            journal.write_bytes(original[journal.name])
            assert report(destination) and snapshot(destination) == original

        zero = root / 'zero'
        seed(zero, 'zero')
        result = report(zero, cycles=1)
        assert 'complete (4000/4000 GPU-ms), covered energy 0.000000000 kWh (exact 0 W-ms)' in result

        old = rate_file('rate notes \u7535', boundary - 20000, boundary + 5000, '0.12')
        negative = rate_file('rate notes second', boundary + 5000, boundary + 12000, '-0.06')
        free = rate_file('rate notes zero', boundary + 14000, boundary + 22000, '0')
        narrow = rate_file('rate notes four seconds', boundary + 12000, boundary + 16000, '9')
        mixed = rate_file('rate notes currency', boundary + 5000, boundary + 12000, '0.2', currency='GBP')
        missing = rate_file('rate notes later', boundary - 20000, boundary + 14000, '0.5', confirmed=boundary + 13000)
        disabled = rate_file('rate notes disabled', boundary - 20000, boundary + 14000, '0.12', enabled=False)
        future = rate_file('rate notes future', boundary - 20000, boundary + 14000, '0.12',
                           confirmed=253402300799000)
        for shape in ('priced-v1', 'priced-v2'):
            destination = root / shape
            seed(destination, shape)
            original = snapshot(destination)
            result = report(destination, at=boundary + 25000, rates=(free, negative, old))
            assert (f'Period [{begin}, {boundary}) through {boundary}: partial (20000/' in result and
                    '0.000100000 USD (exact 360000/3600000000 USD)' in result and
                    'partial (40000/50000 GPU-ms), covered energy 0.000833333 kWh' in result and
                    'Priced coverage 20000/50000 GPU-ms, estimate 0.000015000 USD (exact 54000/3600000000 USD)' in result), result
            result = report(destination, at=boundary + 25000, cycles=1, rates=(negative,))
            assert 'Priced coverage 6000/50000 GPU-ms, estimate -0.000015000 USD (exact -54000/3600000000 USD)' in result, result
            result = report(destination, at=boundary + 25000, cycles=1, rates=(free,))
            assert 'Priced coverage 8000/50000 GPU-ms, estimate 0.000000000 USD (exact 0/3600000000 USD)' in result, result
            result = report(destination, at=boundary + 25000, cycles=1, rates=(narrow,))
            assert 'Priced coverage 0/50000 GPU-ms, estimate unavailable' in result, result
            result = report(destination, at=boundary + 10000, cycles=1, rates=(missing,))
            assert 'Priced coverage 0/20000 GPU-ms, estimate unavailable' in result and 'exact 3000000 W-ms' in result, result
            reset = report(destination, at=boundary, rates=(old, negative))
            assert reset.count('Priced coverage ') == 1 and 'no elapsed time' in reset, reset
            assert snapshot(destination) == original
        assert 'Priced coverage 0/4000 GPU-ms, estimate unavailable' in report(empty, cycles=1, rates=(old,))
        for rates, error in (((old, mixed), 'one currency'), ((old, old), 'effective windows overlap'),
                             ((disabled,), 'disabled or invalid'), ((future,), 'confirmation is in the future')):
            report(root / 'priced-v2', rates=rates, code=3, message=error)
        # More than twelve exact helper ranges, while emitting only twelve reset periods.
        many = tuple(rate_file(f'rate notes range {i}', boundary - 238_000 + 10000 * i,
                               boundary - 232_000 + 10000 * i, '1') for i in range(24))
        multiple = report(root / 'priced-v2', at=boundary + 25000, cycles=12, rates=many)
        assert multiple.count('Period [') == 12 and multiple.count('exact 3000000 W-ms') == 2, multiple
        assert 'estimate 0.000166667 USD (exact 600000/3600000000 USD)' in multiple, multiple

        file.write_text(json.dumps(dict(bimonthly_start_month=2, bimonthly_day=1, clock=clock)), encoding='ascii')
        end_two = utc('2024-05-31T23:00:00' if tzdb else '2024-06-01T00:00:00')
        two_months = report(root / 'priced-v2', at=boundary + 10000, rates=(old, negative))
        assert f'Period [{utc("2024-02-01T00:00:00")}, {boundary})' in two_months, two_months
        assert f'Period [{boundary}, {end_two})' in two_months and 'exact 54000/3600000000 USD' in two_months, two_months

        file.write_text(json.dumps(dict(annual_month=2, annual_day=29,
                        clock=dict(kind='utc', fixed_offset_seconds=0))), encoding='ascii')
        annual = report(root / 'priced-v2', at=boundary + 10000, cycles=2, rates=(old, negative))
        assert f'Period [{utc("2023-02-28T00:00:00")}, {utc("2024-02-29T00:00:00")})' in annual, annual
        assert f'Period [{utc("2024-02-29T00:00:00")}, {utc("2025-02-28T00:00:00")})' in annual, annual
        assert 'exact 6000000 W-ms' in annual and 'exact 414000/3600000000 USD' in annual, annual
        policy()

        for shape in ('prepared', 'committed'):
            destination = root / shape
            seed(destination, shape)
            original = snapshot(destination)
            report(destination, code=3, message='require recovery review')
            report(destination, rates=(old,), code=3, message='require recovery review')
            assert snapshot(destination) == original

        destination = root / 'v2'
        original = snapshot(destination)
        for shape in ('hold', 'hold-legacy', 'hold-operation'):
            with subprocess.Popen([fixture, destination, '0', shape], cwd=root, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, text=True) as holder:
                try:
                    assert holder.stdout.readline() == 'ready\n'
                    report(destination, code=3, message='energy collection is active')
                    assert snapshot(destination) == original and holder.poll() is None
                finally:
                    holder.communicate('\n', timeout=5)
                assert holder.returncode == 0
        assert report(destination)

        if os.name == 'nt':
            physical = root / 'physical'
            physical.mkdir()
            alias = root / 'aliased'
            subprocess.run(['cmd.exe', '/d', '/c', 'mklink', '/J', str(alias), str(physical)], cwd=root,
                           check=True, capture_output=True, timeout=8)
            state_via_alias = alias / 'nested state'
            seed(state_via_alias, 'v2')
            assert report(state_via_alias).count('exact 600000 W-ms') == 2

        full = Path('/dev/full')
        if os.name != 'nt' and full.is_char_device():
            with full.open('wb') as sink:
                blocked = subprocess.run([host, 'energy-periods', '--state-dir', destination,
                                          '--through-utc-ms', str(through), '--reset-policy', file],
                                         cwd=root, stdout=sink, stderr=subprocess.PIPE, text=True, timeout=25)
                assert blocked.returncode == 3 and 'report could not be written' in blocked.stderr

        missing = installed / collector.name
        moved = installed / ('other-' + collector.name)
        missing.rename(moved)
        try:
            report(destination, code=3, message='verified local energy history unavailable')
        finally:
            moved.rename(missing)
        missing.rename(moved)
        try:
            shutil.copy2(installed / auth.name, missing)
            report(destination, code=3, message='verified local energy history unavailable')
            assert snapshot(destination) == original
        finally:
            missing.unlink(missing_ok=True)
            moved.rename(missing)

        tariff_path = root / 'rate notes tariff \u7535.json'
        capture_path = root / 'rate notes capture \u4ef7.json'
        octopus = ('--tariff', tariff_path, '--octopus-rates', capture_path)
        for args in (octopus[:2], octopus[2:], octopus * 2):
            report(extra=args, code=2, message='invalid options')
        report(extra=octopus, rates=(old,), code=2, message='invalid options')

        if tzdb:
            def write_json(path, doc):
                path.write_text(json.dumps(doc, separators=(',', ':')), encoding='ascii')

            def tariff(begin_at, end_at, product):
                declaration = plan(begin_at, begin_at, end_at, clock)
                declaration['plan'].update(territory='C', supplier='Octopus Energy', product=product,
                                           currency='GBP', rounding='meter_import_half_hour_kwh_half_even_2')
                declaration['plan']['revisions'][0].update(time_mode='dynamic', base_rate_per_kwh='0')
                write_json(tariff_path, declaration)

            def capture(product, observed_at, unit, standing, region='C'):
                write_json(capture_path, dict(schema_version=1, product_code=product,
                           tariff_code=f'E-1R-{product}-{region}', payment_method=PAYMENT,
                           observed_utc_ms=observed_at, unit_rate_pages=pages(list(reversed(unit))),
                           standing_charge_pages=pages(standing)))

            observed = boundary + HOUR
            tariff(begin, boundary, PRODUCT)
            slots = (boundary - begin) // HALF_HOUR
            assert slots == 1486
            unit = [row(begin + i * HALF_HOUR, begin + (i + 1) * HALF_HOUR,
                        -10 if i == slots - 1 else 11 + i % 2) for i in range(slots)]
            fee = [row(begin, None, 1000)]
            capture(PRODUCT, observed, unit, fee)
            inputs = [p.read_bytes() for p in (tariff_path, capture_path)]
            for shape in ('priced-v1', 'priced-v2'):
                directory = root / shape
                saved = snapshot(directory)
                result = report(directory, at=observed, extra=octopus)
                assert 'Captured Octopus Agile GPU-board energy-only estimates (GBP)' in result, result
                assert f'Price capture observed UTC ms {observed}' in result, result
                assert 'exact 3000000 W-ms' in result and 'exact -240000/3600000000 GBP' in result, result
                assert ('estimate -0.000066667 GBP' in result and 'estimate unavailable' in result and
                        'Priced coverage 16000/' in result and 'Priced coverage 0/7200000 GPU-ms' in result), result
                assert snapshot(directory) == saved
            assert [p.read_bytes() for p in (tariff_path, capture_path)] == inputs
            for shape in ('prepared', 'committed'):
                directory = root / shape
                saved = snapshot(directory)
                report(directory, at=observed, extra=octopus, code=3, message='require recovery review')
                assert snapshot(directory) == saved
            report(root / 'priced-v2', at=observed - 1, extra=octopus, code=3, message='observed after')
            capture(PRODUCT, observed, unit[1:], fee)
            report(root / 'priced-v2', at=observed, extra=octopus, code=3, message='every exact half hour')
            capture(PRODUCT, observed, unit, fee, region='D')
            report(root / 'priced-v2', at=observed, extra=octopus, code=3, message='rate identity')
            capture(PRODUCT, observed, unit, [])
            report(root / 'priced-v2', at=observed, extra=octopus, code=3, message='incomplete')

            # A Go price spanning a midnight fee change has no unit-rate boundary at that instant.
            tariff(begin, boundary, GO_PRODUCT)
            fee_change = utc('2024-03-23T00:00:00')
            go_units = [row(begin - HALF_HOUR, boundary + HALF_HOUR, 12.5, PAYMENT),
                        row(begin, boundary, 99, 'NON_DIRECT_DEBIT')]
            capture(GO_PRODUCT, observed, go_units,
                    [row(begin, fee_change, 10), row(fee_change, None, 50)])
            go = root / 'go'
            seed(go, 'priced-v2', at=fee_change)
            result = report(go, at=observed, extra=octopus)
            assert 'Captured Octopus Go' in result and 'Priced coverage 60000/' in result, result
            assert 'exact 750000/3600000000 GBP' in result and 'estimate 0.000208333 GBP' in result, result

            # Near the maximum supported capture size. This crosses the old request and response byte limits.
            long_begin, long_end = utc('2024-01-01T00:00:00'), utc('2024-06-18T23:00:00')
            long_slots = (long_end - long_begin) // HALF_HOUR
            assert 8100 <= long_slots <= 8192
            tariff(long_begin, long_end, PRODUCT)
            capture(PRODUCT, long_end + HOUR,
                    [row(long_begin + i * HALF_HOUR, long_begin + (i + 1) * HALF_HOUR, i % 2)
                     for i in range(long_slots)], [row(long_begin, None, 10)])
            write_json(file, dict(annual_month=1, annual_day=1, clock=clock))
            result = report(root / 'priced-v2', at=long_end + HOUR, cycles=1, extra=octopus)
            assert 'Audited sequence 2' in result and 'exact 6000000 W-ms' in result, result
            assert 'Priced coverage 52000/' in result and 'exact 24000/3600000000 GBP' in result, result
            policy(clock={'kind': 'utc', 'fixed_offset_seconds': 0})
            result = report(destination, at=utc('2024-04-01T00:00:02'))
            assert 'exact 1200000 W-ms' in result and result.count(': unavailable') == 1
            policy()
            (installed / BUNDLE / 'version').write_text('wrong', encoding='ascii')
            report(destination, code=3, message='monthly reset periods unavailable')
            policy(clock={'kind': 'utc', 'fixed_offset_seconds': 0})
            report(extra=octopus, code=3, message='reviewed tariff time database unavailable')
            assert not state.exists()
        else:
            policy(day=31, clock={'kind': 'fixed_standard', 'fixed_offset_seconds': -18000})
            result = report(destination, at=utc('2024-02-29T05:00:01'), cycles=2)
            february = utc('2024-02-29T05:00:00')
            assert f'Period [{utc("2024-01-31T05:00:00")}, {february})' in result
            assert f'Period [{february}, {utc("2024-03-31T05:00:00")})' in result

        print(f'energy periods: {counts} command invocations, one committed snapshot per report')


if __name__ == '__main__':
    main(*(Path(p).resolve(strict=True) for p in sys.argv[1:]))
