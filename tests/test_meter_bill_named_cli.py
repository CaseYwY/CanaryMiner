"""Exercise the native meter-bill entry point and its installed, version-bound IANA input."""
import copy
import datetime as dt
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
BUNDLE = 'canary-tzdb-2026e'
SCOPE = 'synthetic-private-meter'
HOUR = 3600000


def utc(text):
    return int(dt.datetime.fromisoformat(text).replace(tzinfo=dt.timezone.utc).timestamp()) * 1000


def named(zone):
    return {'kind': 'named_iana', 'zone': zone, 'time_database_version': '2026e'}


def plan(begin, through, end, rate_clock, policy='explicit_cycle_only', rates_end=None):
    original = json.loads((ROOT / 'config/tariff-declaration.example.json').read_text())
    original.update(schema_version=4, confirmed_utc=max(1, begin // 1000), reset_policy=policy)
    original['plan'].update(clock=rate_clock, tiers='none', cycle_charge='0', subtotal_multiplier='1')
    original['plan']['revisions'][0].update(
        begin_utc_ms=begin, end_utc_ms=rates_end or end, published_utc_ms=begin,
        time_mode='constant', base_rate_per_kwh='1', charge_per_civil_day='0',
        calendar=[], dynamic=[], bands=[])
    original['opening'].update(
        scope=SCOPE, cycle_begin_utc_ms=begin, cycle_end_utc_ms=end, through_utc_ms=through,
        imported_kwh='0', accrued_subtotal='0', time_database_version=rate_clock.get(
            'time_database_version', 'explicit-fixed-clock'))
    return original


def interval(begin, end, watts='1000'):
    return {'schema_version': 1, 'scope': SCOPE, 'intervals': [
        {'begin_utc_ms': begin, 'end_utc_ms': end, 'average_watts': watts}
    ]}


def register(points):
    return {'schema_version': 2, 'scope': SCOPE, 'register_readings': [
        {'utc_ms': t, 'imported_kwh': kwh} for t, kwh in points
    ]}


def calendar(document, start_minute, end_minute):
    rate = document['plan']['revisions'][0]
    rate.update(time_mode='calendar', charge_per_civil_day='2', calendar=[{
        'key': 'synthetic-change', 'begin_minute': start_minute, 'end_minute': end_minute,
        'weekday_mask': 127, 'month_mask': 4095, 'priority': 1, 'date': None,
        'published_utc_ms': rate['published_utc_ms'], 'rate_per_kwh': '3',
    }])


def main(cli, bundle):
    with tempfile.TemporaryDirectory(prefix='canary-named-tariff-') as tmp:
        root = Path(tmp)
        package = root / 'installed-\u65f6\u533a'
        package.mkdir()
        binary = Path(shutil.copy2(cli, package / cli.name))
        tariff, meter = root / 'private rate.json', root / 'private meter.json'
        inputs = ['--tariff', tariff.name, '--intervals', meter.name]
        zone = named('America/New_York')
        spring = utc('2024-03-10T05:00:00')
        spring_end = utc('2024-03-11T04:00:00')
        fall = utc('2024-11-03T04:00:00')
        fall_end = utc('2024-11-04T05:00:00')
        calls = 0

        def store(declaration, readings):
            tariff.write_text(json.dumps(declaration), encoding='utf-8')
            meter.write_text(json.dumps(readings), encoding='utf-8')

        def run(*args):
            nonlocal calls
            calls += 1
            originals = [(p, p.read_bytes()) for p in (tariff, meter) if p.exists()]
            result = subprocess.run([binary, 'meter-bill', *args], cwd=root, capture_output=True, text=True, timeout=10)
            for private in (tariff.name, meter.name, SCOPE, str(package), 'FORBIDDEN_DEVICE_SETUP'):
                assert private not in result.stderr + result.stdout, private
            for path, data in originals:
                assert path.read_bytes() == data
            return result

        def review(end, *options):
            return run(*inputs, '--as-of-utc-ms', str(end), *options)

        def reject(result, reason):
            assert result.returncode == 3 and not result.stdout and reason in result.stderr, result

        def accept(result, *fragments):
            assert result.returncode == 0 and not result.stderr, result
            for fragment in fragments:
                assert fragment in result.stdout, (fragment, result.stdout)
            return result.stdout

        spring_plan = plan(spring, spring, spring_end, zone)
        calendar(spring_plan, 120, 180)
        store(spring_plan, interval(spring, spring_end))

        # The current directory cannot supply a missing installed dataset.
        shutil.copytree(bundle, root / BUNDLE)
        reject(review(spring_end), 'reviewed tariff time database unavailable or invalid')
        installed = Path(shutil.copytree(bundle, package / BUNDLE))
        accept(review(spring_end), 'Reviewed IANA time database 2026e',
               'Imported usage 23.000000 kWh', 'Cycle estimate at closing 25.000000 USD')
        fall_plan = plan(fall, fall, fall_end, zone)
        calendar(fall_plan, 60, 120)
        store(fall_plan, interval(fall, fall_end))
        accept(review(fall_end), 'Imported usage 25.000000 kWh', 'Cycle estimate at closing 31.000000 USD')
        store(fall_plan, register([(fall, '0'), (fall_end, '25')]))
        reject(review(fall_end), 'energy-rate boundary')
        store(fall_plan, register([(fall, '0'), (fall+HOUR, '1'), (fall+3*HOUR, '3'), (fall_end, '25')]))
        saved = accept(review(fall_end, '--save-review', 'fall.cmr'), 'Cycle estimate at closing 31.000000 USD')
        archive = root / 'fall.cmr'
        original = archive.read_bytes()
        tariff.unlink()
        meter.unlink()
        assert accept(run('--open-review', archive.name), '2026e') == saved

        # Independent reset zone with a UTC price clock. The closing instant uses the DST offset.
        month_begin = utc('2024-03-01T05:00:00')
        month_end = utc('2024-04-01T04:00:00')
        may = utc('2024-05-01T04:00:00')
        utc_clock = {'kind': 'utc', 'fixed_offset_seconds': 0}
        monthly = plan(month_begin, month_end-HOUR, month_end, utc_clock,
                       {'monthly_day': 1, 'clock': zone}, rates_end=may)
        store(monthly, interval(month_end-HOUR, month_end+HOUR))
        out = accept(review(month_end+HOUR), f'Cycle 2 [{month_end}, {may}) UTC ms')
        assert out.count('Imported usage 1.000000 kWh') == 2
        named_monthly = copy.deepcopy(monthly)
        named_monthly['plan']['clock'] = zone
        named_monthly['opening']['time_database_version'] = '2026e'
        store(named_monthly, interval(month_end-HOUR, month_end+HOUR))
        accept(review(month_end+HOUR), f'Cycle 2 [{month_end}, {may}) UTC ms')
        monthly['opening']['cycle_end_utc_ms'] += HOUR
        store(monthly, interval(month_end-HOUR, month_end+HOUR))
        reject(review(month_end+HOUR), 'saved tariff declaration unavailable or invalid')

        # March 10 is 23 hours at the price clock. A UTC reset allocates 19 and 4 elapsed hours.
        begin = utc('2024-02-11T00:00:00')
        boundary = utc('2024-03-11T00:00:00')
        end = utc('2024-04-11T00:00:00')
        fee_plan = plan(begin, spring, boundary, zone, {
            'monthly_day': 11, 'clock': utc_clock, 'daily_fee_allocation': 'prorate_by_cycle_minutes'
        }, rates_end=end)
        fee_plan['plan']['revisions'][0]['charge_per_civil_day'] = '23'
        for amount, reason in [('23', None), ('1', 'unsupported rule')]:
            fee_plan['plan']['revisions'][0]['charge_per_civil_day'] = amount
            store(fee_plan, interval(spring, boundary+HOUR, '0'))
            result = review(boundary+HOUR)
            if reason:
                reject(result, reason)
            else:
                accept(result, 'Cycle estimate at closing 19.000000 USD',
                       'Provisional cycle estimate 4.000000 USD')

        # The 25-hour fall day splits into 20 and five hours; even a unit fee has exact shares.
        fall_boundary = utc('2024-11-04T00:00:00')
        fall_fee = plan(utc('2024-10-04T00:00:00'), fall, fall_boundary, zone, {
            'monthly_day': 4, 'clock': utc_clock, 'daily_fee_allocation': 'prorate_by_cycle_minutes'
        }, rates_end=utc('2024-12-04T00:00:00'))
        fall_fee['plan']['revisions'][0]['charge_per_civil_day'] = '1'
        store(fall_fee, interval(fall, fall_boundary+HOUR, '0'))
        accept(review(fall_boundary+HOUR), 'Cycle estimate at closing 0.800000 USD',
               'Provisional cycle estimate 0.200000 USD')

        # Newfoundland's old midnight rollback re-entered the prior date after its first end.
        overlap = utc('2010-11-07T02:45:00')
        overlap_end = utc('2010-11-07T04:00:00')
        st_johns = named('America/St_Johns')
        for start, end in [(overlap, overlap_end), (utc('2010-11-07T03:45:00'), overlap_end),
                           (utc('2010-11-06T15:00:00'), utc('2010-11-06T18:00:00'))]:
            overlap_plan = plan(start, start, end, st_johns)
            overlap_plan['plan']['revisions'][0]['charge_per_civil_day'] = '1'
            store(overlap_plan, interval(start, end))
            reject(review(end), 'unsupported rule')
        first_reset = utc('2010-11-07T02:30:00')
        next_reset = utc('2010-12-07T03:30:00')
        overlap_reset = plan(first_reset, overlap, next_reset, utc_clock,
                             {'monthly_day': 7, 'clock': st_johns})
        store(overlap_reset, interval(overlap, overlap_end))
        accept(review(overlap_end), f'Cycle 1 [{first_reset}, {next_reset}) UTC ms')

        # A skipped local date is not a legitimate monthly reset anchor.
        skipped = plan(utc('2011-11-30T10:00:00'), utc('2011-12-29T11:00:00'),
                       utc('2011-12-30T10:00:00'), utc_clock,
                       {'monthly_day': 30, 'clock': named('Pacific/Apia')})
        store(skipped, interval(skipped['opening']['through_utc_ms'], skipped['opening']['cycle_end_utc_ms']))
        reject(review(skipped['opening']['cycle_end_utc_ms']), 'saved tariff declaration unavailable or invalid')

        for key, value in [('zone', 'Never/A_Zone'), ('time_database_version', '2026a')]:
            invalid = copy.deepcopy(spring_plan)
            invalid['plan']['clock'][key] = value
            store(invalid, interval(spring, spring_end))
            reject(review(spring_end), 'saved tariff declaration unavailable or invalid')

        for changed in ['systemv', 'northamerica']:
            file = installed / changed
            before = file.read_bytes() if file.exists() else None
            file.write_bytes(b'# changed\\n')
            reject(run('--open-review', archive.name), 'reviewed tariff time database unavailable or invalid')
            assert archive.read_bytes() == original
            if before is None:
                file.unlink()
            else:
                file.write_bytes(before)

        # UTC remains usable independently of a damaged optional package.
        (installed / 'version').write_text('2026a\\n', encoding='ascii')
        fixed = plan(spring, spring, spring_end, utc_clock)
        fixed['schema_version'] = 1
        store(fixed, interval(spring, spring_end))
        accept(review(spring_end), 'Cycle estimate at closing 23.000000 USD')
    print(f'named tariff CLI: {calls} cases with local pinned data, DST rates, monthly resets, exact fees and archive refusal')


if __name__ == '__main__':
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
