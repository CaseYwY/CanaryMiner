"""Exercise the ordinary meter-bill command using local synthetic input files."""
import copy
import datetime
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
MARCH = 1709251200000
APRIL = 1711929600000
MAY = 1714521600000
NOON = 43200000
SCOPE = 'synthetic-private-meter-scope'


def main():
    cli = Path(sys.argv[1]).resolve()
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-meter-bill-') as directory:
        root = Path(directory)
        tariff = root / 'private rate.json'
        intervals = root / 'meter interval file.json'

        def launch(*args):
            nonlocal checks
            before = {path.name: path.read_bytes() for path in (tariff, intervals) if path.is_file()}
            names = {entry.name for entry in root.iterdir()}
            result = subprocess.run([str(cli), 'meter-bill', *map(str, args)],
                                    cwd=root, capture_output=True, text=True, timeout=8)
            assert 'FORBIDDEN_DEVICE_SETUP' not in result.stdout + result.stderr
            for private in (SCOPE, tariff.name, intervals.name, 'private-option-value'):
                assert private not in result.stdout + result.stderr
            assert {entry.name for entry in root.iterdir()} == names
            assert before == {path.name: path.read_bytes() for path in (tariff, intervals) if path.is_file()}
            checks += 1
            return result

        assert launch('--help').returncode == 0
        for args in ((), ('--tariff', tariff), ('--tariff', tariff, '--tariff', tariff),
                     ('--tariff', tariff, '--pool', 'private-option-value'),
                     ('--tariff', '', '--intervals', intervals), ('--help', '--tariff', tariff),
                     ('--tariff', '--help', '--intervals', intervals)):
            result = launch(*args)
            assert result.returncode == 2 and not result.stdout

        original = json.loads((ROOT / 'config/tariff-declaration.example.json').read_text())
        original.update(schema_version=2, confirmed_utc=MARCH // 1000,
                        reset_policy={'monthly_day': 1, 'clock': {'kind': 'utc', 'fixed_offset_seconds': 0}})
        original['plan']['tiers'] = 'none'
        original['plan']['revisions'][0].update(
            begin_utc_ms=MARCH, end_utc_ms=MAY, published_utc_ms=MARCH, time_mode='constant',
            charge_per_civil_day='0', calendar=[], dynamic=[], bands=[])
        original['opening'].update(scope=SCOPE, cycle_begin_utc_ms=MARCH, cycle_end_utc_ms=APRIL,
                                   through_utc_ms=APRIL-NOON, imported_kwh='21', accrued_subtotal='2')
        reading = {'begin_utc_ms': APRIL-NOON, 'end_utc_ms': APRIL+NOON, 'average_watts': '1000'}
        source = {'schema_version': 1, 'scope': SCOPE, 'intervals': [reading]}

        def store(plan=original, meter=source):
            tariff.write_text(json.dumps(plan), encoding='utf-8')
            intervals.write_text(json.dumps(meter), encoding='utf-8')

        def review(*args, as_of=None):
            cutoff = ('--as-of-utc-ms', as_of) if as_of is not None else ()
            return launch('--tariff', tariff if not args else args[0], '--intervals', intervals if len(args) < 2 else args[1], *cutoff)

        def success(as_of=None):
            result = review(as_of=as_of)
            assert result.returncode == 0 and not result.stderr, f'Expected estimate in case {checks}'
            return result.stdout

        def rejected(reason, as_of=None):
            result = review(as_of=as_of)
            assert result.returncode == 3 and not result.stdout and reason in result.stderr, f'Refusal mismatch in case {checks}'

        rejected('saved tariff declaration')
        store()
        text = success()
        assert 'uniformly within each interval' in text
        assert text.count('Imported usage 12.000000 kWh') == 2
        assert 'Change from imported readings 2.640000 USD' in text
        assert 'Cycle estimate at closing 4.840000 USD (includes declared opening)' in text
        assert 'Provisional cycle estimate 4.840000 USD' in text
        assert f'Reviewed interval range [{APRIL}, {APRIL+NOON}) UTC ms' in text

        progressive = copy.deepcopy(original)
        progressive['plan']['tiers'] = 'progressive'
        progressive['plan']['revisions'][0]['bands'] = [
            {'through_kwh': '10', 'rate_per_kwh': '0.05'},
            {'through_kwh': None, 'rate_per_kwh': '0.15'},
        ]
        store(progressive)
        text = success()
        assert 'Cycle estimate at closing 6.820000 USD' in text
        assert 'Provisional cycle estimate 5.720000 USD' in text

        all_units = copy.deepcopy(progressive)
        all_units['plan'].update(tiers='all_units', minimum_cycle_subtotal='8')
        store(all_units)
        text = success()
        assert 'Cycle estimate at closing 8.800000 USD' in text
        assert 'Provisional cycle estimate 6.820000 USD' in text
        assert text.count('Closing charges depend on full-cycle usage.') == 1

        later_rate = copy.deepcopy(original)
        prior = later_rate['plan']['revisions'][0]
        prior['end_utc_ms'] = APRIL
        changed = {**prior, 'key': 'synthetic-later', 'begin_utc_ms': APRIL, 'end_utc_ms': MAY, 'base_rate_per_kwh': '0.4'}
        later_rate['plan']['revisions'].append(changed)
        store(later_rate)
        assert 'Provisional cycle estimate 7.480000 USD' in success()

        cutoff = APRIL + NOON
        replay = success(as_of=cutoff)
        assert f'Review cutoff {cutoff} UTC ms' in replay
        assert 'Provisional cycle estimate 7.480000 USD' in replay
        assert success(as_of=cutoff) == replay
        rejected('after the review cutoff', as_of=cutoff - 1)
        rejected('review cutoff is in the future', as_of=253402300799999)
        for invalid in ('0', '01', '+1', '1.0', '9x', '253402300800000'):
            result = review(as_of=invalid)
            assert result.returncode == 2 and not result.stdout
        result = launch('--tariff', tariff, '--as-of-utc-ms', cutoff, '--as-of-utc-ms', cutoff)
        assert result.returncode == 2 and not result.stdout
        fullwidth_digits = '\uff10\uff11\uff12\uff13\uff14\uff15\uff16\uff17\uff18\uff19'
        fullwidth_cutoff = str(cutoff).translate(str.maketrans('0123456789', fullwidth_digits))
        for flag, value in (('--as-of-utc-ms', fullwidth_cutoff), ('\uff0d\uff0das-of-utc-ms', cutoff)):
            result = launch('--tariff', tariff, '--intervals', intervals, flag, value)
            assert result.returncode in (2, 3) and not result.stdout

        # Historical cutoff is separate from today's declaration confirmation. The
        # affected cycle has readings, but its price was still unpublished at cutoff.
        late_publication = copy.deepcopy(later_rate)
        release_time = APRIL + 2 * NOON
        late_publication['confirmed_utc'] = release_time // 1000
        late_publication['plan']['revisions'][1]['published_utc_ms'] = release_time
        store(late_publication)
        rejected('rate published after decision instant', as_of=cutoff)
        assert f'Review cutoff {release_time} UTC ms' in success(as_of=release_time)

        dynamic = copy.deepcopy(original)
        dynamic['plan']['revisions'][0].update(
            time_mode='dynamic', base_rate_per_kwh='0',
            dynamic=[{'begin_utc_ms': MARCH, 'end_utc_ms': APRIL, 'published_utc_ms': MARCH, 'rate_per_kwh': '0.2'}])
        store(dynamic)
        rejected('dynamic price gap')

        legacy = {**original, 'schema_version': 1, 'reset_policy': 'explicit_cycle_only'}
        store(legacy)
        rejected('next cycle')
        ends_at_reset = {**source, 'intervals': [{**reading, 'end_utc_ms': APRIL}]}
        store(legacy, ends_at_reset)
        text = success()
        assert 'Cycle 2 ' not in text and 'Cycle estimate at closing 4.840000 USD' in text
        store(original, {**source, 'intervals': [{**reading, 'average_watts': '0'}]})
        assert 'Imported usage 0.000000 kWh' in success()

        split_day = copy.deepcopy(original)
        offset = 19800000
        split_day['reset_policy']['clock'] = {'kind': 'fixed_standard', 'fixed_offset_seconds': 19800}
        split_day['opening'].update(cycle_begin_utc_ms=MARCH-offset, cycle_end_utc_ms=APRIL-offset)
        split_day['plan']['revisions'][0].update(begin_utc_ms=MARCH-offset, charge_per_civil_day='0.1')
        store(split_day)
        rejected('daily fee')

        boundary = APRIL - offset
        # A fee revision at the cycle reset does not change the fee already set at tariff midnight.
        prorated = copy.deepcopy(split_day)
        prorated['schema_version'] = 3
        prorated['reset_policy']['daily_fee_allocation'] = 'prorate_by_cycle_minutes'
        prorated['plan'].update(cycle_charge='0', subtotal_multiplier='1')
        before_reset = prorated['plan']['revisions'][0]
        before_reset.update(end_utc_ms=boundary, charge_per_civil_day='1.44')
        prorated['plan']['revisions'].append({
            **before_reset, 'key': 'synthetic-reset-revision', 'begin_utc_ms': boundary,
            'end_utc_ms': MAY, 'charge_per_civil_day': '9.99',
        })
        tariff_midnight = APRIL - 2 * NOON
        prorated['opening'].update(through_utc_ms=tariff_midnight, imported_kwh='0', accrued_subtotal='0')
        one_day = {**source, 'intervals': [
            {'begin_utc_ms': tariff_midnight, 'end_utc_ms': boundary+3600000, 'average_watts': '0'},
        ]}
        store(prorated, one_day)
        text = success(as_of=boundary+3600000)
        assert 'split by minutes in each billing cycle' in text
        assert 'Cycle estimate at closing 1.110000 USD' in text
        assert 'Provisional cycle estimate 0.330000 USD' in text
        # The next civil midnight selects the newly effective daily fee.
        one_day['intervals'][0]['end_utc_ms'] = APRIL + 1
        store(prorated, one_day)
        assert 'Provisional cycle estimate 10.320000 USD' in success(as_of=APRIL+1)

        def register(rows):
            return {'schema_version': 2, 'scope': SCOPE,
                    'register_readings': [{'utc_ms': at, 'imported_kwh': amount} for at, amount in rows]}

        start = APRIL-NOON
        supplied = register([(start, '248129.75'), (APRIL, '248141.75'), (APRIL+NOON, '248153.75')])
        store(original, supplied)
        text = success(as_of=APRIL+NOON)
        assert 'exact differences between cumulative import readings' in text
        assert 'uniformly' not in text and '248129.75' not in text
        assert text.count('Imported usage 12.000000 kWh') == 2 and 'Cycle estimate at closing 4.840000 USD' in text
        assert 'Provisional cycle estimate 4.840000 USD' in text
        rejected('after the review cutoff', as_of=APRIL+NOON-1)

        for invalid, message in (
            (register([(start, '10'), (APRIL+1, '11')]), 'billing-cycle boundary'),
            (register([(start+1, '10'), (start+2, '11')]), 'match the saved opening time'),
            (register([(start, '10'), (start-1, '11')]), 'timestamps must strictly increase'),
            (register([(start, '10'), (start, '11')]), 'timestamps must strictly increase'),
            (register([(start, '10'), (start+1, '9')]), 'reset or rollover'),
            (register([(start, '10'), (start+1, '10.00')]), 'invalid meter register reading'),
            (register([(start, '10'), (start+1, -1)]), 'invalid meter register reading'),
            (register([(start, '10')]), 'invalid interval document'),
            ({**supplied, 'scope': 'private-second-meter'}, 'scope differs'),
            (register([(start+i, str(i)) for i in range(8194)]), 'invalid interval document'),
        ):
            store(original, invalid)
            rejected(message)

        epoch = copy.deepcopy(legacy)
        epoch['opening'].update(cycle_begin_utc_ms=0, cycle_end_utc_ms=2*NOON, through_utc_ms=0,
                                imported_kwh='0', accrued_subtotal='0')
        epoch['plan']['revisions'][0].update(begin_utc_ms=0, end_utc_ms=2*NOON, published_utc_ms=0)
        store(epoch, register([(0, '8'), (7, '9')]))
        assert 'Imported usage 1.000000 kWh' in success(as_of=7)

        # Irregular milliseconds and a fractional meter delta never have to form a decimal average wattage.
        short = register([(start, '49.25'), (start+7, '49.5')])
        store(original, short)
        assert 'Imported usage 0.250000 kWh' in success()
        assert 'Change from imported readings 0.055000 USD' in success()

        earlier = copy.deepcopy(original)
        begin = start-2*NOON
        earlier['opening'].update(through_utc_ms=begin)
        earlier['plan']['revisions'][0]['charge_per_civil_day'] = '0.1'
        # A flat rate across a civil midnight can book the next daily fee without allocating the kWh.
        store(earlier, register([(begin, '4'), (start+1, '5')]))
        text = success()
        assert 'Imported usage 1.000000 kWh' in text and 'Change from imported readings 0.330000 USD' in text

        change = copy.deepcopy(original)
        cut = start+7
        first = change['plan']['revisions'][0]
        first['end_utc_ms'] = cut
        second = {**first, 'key': 'other-units', 'begin_utc_ms': cut, 'end_utc_ms': MAY}
        change['plan']['revisions'].append(second)
        combined = register([(start, '0'), (cut+7, '2')])
        store(change, combined)
        assert 'Change from imported readings 0.440000 USD' in success()
        second['base_rate_per_kwh'] = '0.5'
        store(change, combined)
        rejected('energy-rate boundary')
        store(change, register([(start, '0'), (cut, '1'), (cut+7, '2')]))
        assert 'Change from imported readings 0.770000 USD' in success()
        store(change, register([(start, '1'), (cut+7, '1')]))
        assert 'Imported usage 0.000000 kWh' in success()

        second['base_rate_per_kwh'] = first['base_rate_per_kwh']
        change['plan']['tiers'] = 'progressive'
        change['opening']['imported_kwh'] = '9'
        first['bands'] = [{'through_kwh': '10', 'rate_per_kwh': '0.05'},
                          {'through_kwh': None, 'rate_per_kwh': '0.1'}]
        second['bands'] = copy.deepcopy(first['bands'])
        store(change, combined)
        assert 'Change from imported readings 0.605000 USD' in success()
        second['bands'][1]['rate_per_kwh'] = '0.3'
        store(change, combined)
        rejected('energy-rate boundary')
        store(change, register([(start, '0'), (cut, '1'), (cut+7, '2')]))
        assert 'Change from imported readings 0.825000 USD' in success()

        prices = copy.deepcopy(original)
        prices['confirmed_utc'] = (cut+1000) // 1000
        prices['plan']['revisions'][0].update(
            time_mode='dynamic', base_rate_per_kwh='0',
            dynamic=[
                {'begin_utc_ms': MARCH, 'end_utc_ms': cut, 'published_utc_ms': MARCH, 'rate_per_kwh': '0.2'},
                {'begin_utc_ms': cut, 'end_utc_ms': MAY, 'published_utc_ms': cut, 'rate_per_kwh': '-0.1'},
            ])
        store(prices, combined)
        rejected('energy-rate boundary')
        store(prices, register([(start, '0'), (cut, '1'), (cut+7, '2')]))
        assert 'Change from imported readings 0.110000 USD' in success(as_of=cut+7)
        no_future = copy.deepcopy(prices)
        no_future['plan']['revisions'][0]['dynamic'][1]['published_utc_ms'] = cut+8
        store(no_future, register([(start, '0'), (cut, '1'), (cut+7, '2')]))
        rejected('rate published after decision instant', as_of=cut+7)

        daily_register = register([(tariff_midnight, '20'), (boundary, '20'), (boundary+7, '20')])
        store(prorated, daily_register)
        text = success(as_of=boundary+7)
        assert 'Cycle estimate at closing 1.110000 USD' in text and 'Provisional cycle estimate 0.330000 USD' in text

        historical_split = copy.deepcopy(split_day)
        announcement = boundary + 3 * 3600000
        fee_start = boundary + 4 * 3600000
        historical_split['confirmed_utc'] = announcement // 1000
        before_fee = historical_split['plan']['revisions'][0]
        before_fee.update(end_utc_ms=fee_start, charge_per_civil_day='0')
        historical_split['plan']['revisions'].append(
            {**before_fee, 'key': 'synthetic-later-daily-fee', 'begin_utc_ms': fee_start, 'end_utc_ms': MAY,
             'published_utc_ms': announcement, 'charge_per_civil_day': '0.1'})
        early_cutoff = boundary + 2 * 3600000
        store(historical_split, {**source, 'intervals': [{**reading, 'end_utc_ms': early_cutoff}]})
        text = success(as_of=early_cutoff)
        assert f'Review cutoff {early_cutoff} UTC ms' in text and 'Cycle 2 ' in text
        rejected('daily fee', as_of=announcement)
        rejected('daily fee')

        long_plan = copy.deepcopy(original)
        end = int(datetime.datetime(2025, 4, 2, tzinfo=datetime.timezone.utc).timestamp() * 1000)
        may_2025 = int(datetime.datetime(2025, 5, 1, tzinfo=datetime.timezone.utc).timestamp() * 1000)
        long_plan['plan']['revisions'][0]['end_utc_ms'] = may_2025
        store(long_plan, {**source, 'intervals': [{**reading, 'end_utc_ms': end}]})
        rejected('more than 12 cycles')

        for invalid, message in (
            ({**source, 'schema_version': 2}, 'invalid interval document'),
            ({**source, 'intervals': []}, 'invalid interval document'),
            ({**source, 'scope': SCOPE+'-other'}, 'scope differs'),
            ({**source, 'intervals': [{**reading, 'begin_utc_ms': APRIL-NOON+1}]}, 'gap or overlap'),
            ({**source, 'intervals': [{**reading, 'begin_utc_ms': APRIL-NOON-1}]}, 'gap or overlap'),
            ({**source, 'intervals': [reading, reading]}, 'gap or overlap'),
            ({**source, 'intervals': [{**reading, 'end_utc_ms': 253402300799999}]}, 'future'),
            ({**source, 'intervals': [{**reading, 'end_utc_ms': 253402300800000}]}, 'invalid meter interval'),
            ({**source, 'intervals': [{**reading, 'average_watts': '1e3'}]}, 'invalid meter interval'),
            ({**source, 'intervals': [{**reading, 'average_watts': '-1'}]}, 'invalid meter interval'),
            ({**source, 'intervals': [{**reading, 'average_watts': 1000}]}, 'invalid meter interval'),
            ({**source, 'intervals': [{**reading, 'average_watts': '1000.0'}]}, 'invalid meter interval'),
        ):
            store(original, invalid)
            rejected(message)

        store()
        intervals.write_text('(' + SCOPE, encoding='utf-8')
        rejected('interval document')
        intervals.write_bytes(b' ' * (1024 * 1024 + 1))
        rejected('oversized')
        many_rows = [{'begin_utc_ms': APRIL-NOON+i, 'end_utc_ms': APRIL-NOON+i+1, 'average_watts': '1'}
                     for i in range(8193)]
        store(original, {**source, 'intervals': many_rows})
        rejected('invalid interval document')
        intervals.unlink()
        rejected('interval file unavailable')
        intervals.mkdir()
        rejected('interval file unavailable')
        intervals.rmdir()
        store()
        unicode_dir = root / '\u7535\u8868-\u6d4b\u91cf'
        unicode_dir.mkdir()
        unicode_tariff = unicode_dir / '\u8d39\u7387-\U0001f4c4.json'
        unicode_intervals = unicode_dir / '\u529f\u7387-\U0001f4ca.json'
        unicode_tariff.write_bytes(tariff.read_bytes())
        unicode_intervals.write_bytes(intervals.read_bytes())
        result = review(unicode_tariff, unicode_intervals)
        assert result.returncode == 0 and '4.840000 USD' in result.stdout
        result = review(unicode_tariff, unicode_intervals, as_of=cutoff)
        assert result.returncode == 0 and f'Review cutoff {cutoff} UTC ms' in result.stdout
        assert '4.840000 USD' in result.stdout
        assert unicode_tariff.read_bytes() == tariff.read_bytes()
        assert unicode_intervals.read_bytes() == intervals.read_bytes()
        if os.name == 'posix':
            alias = root / 'alias'
            alias.symlink_to(intervals)
            result = review(tariff, alias)
            assert result.returncode == 3 and not result.stdout
            pipe = root / 'pipe'
            os.mkfifo(pipe)
            result = review(tariff, pipe)
            assert result.returncode == 3 and not result.stdout

            # The long calendar review should still be active once the process has installed its signal handler.
            january, following_january = 1704067200000, 1735689600000
            stopping_plan = copy.deepcopy(original)
            stopping_plan['confirmed_utc'] = january // 1000
            stopping_plan['opening'].update(
                cycle_begin_utc_ms=january, cycle_end_utc_ms=1706745600000,
                through_utc_ms=january, imported_kwh='0', accrued_subtotal='0')
            stopping_plan['plan']['revisions'][0].update(
                begin_utc_ms=january, end_utc_ms=following_january, published_utc_ms=january, time_mode='calendar',
                calendar=[{'key': f'test-rate-{slot}', 'begin_minute': slot*40, 'end_minute': slot*40+20,
                           'weekday_mask': 127, 'month_mask': 4095, 'priority': 1, 'date': None,
                           'published_utc_ms': january, 'rate_per_kwh': '0.3'} for slot in range(32)])
            stopping_source = {**source, 'intervals': [
                {**reading, 'begin_utc_ms': january, 'end_utc_ms': following_january}]}
            store(stopping_plan, stopping_source)
            before = (tariff.read_bytes(), intervals.read_bytes())
            archive_name = 'verified-before-interrupt.cmr'
            import_args = ('--tariff', str(tariff), '--intervals', str(intervals))
            prepared = subprocess.run([str(cli), 'meter-bill', *import_args, '--save-review', archive_name],
                                      cwd=root, capture_output=True, timeout=8)
            assert prepared.returncode == 0 and prepared.stdout and not prepared.stderr
            archive = root / archive_name
            original_archive = archive.read_bytes()
            for args, code, expected_error in (
                (import_args, 0, b''),
                ((*import_args, '--save-review', 'stopped.cmr'), 130,
                 b'meter-bill: archive save stopped before publication\n'),
                (('--open-review', archive_name), 130, b'meter-bill: saved review verification stopped\n'),
            ):
                with subprocess.Popen([str(cli), 'meter-bill', *args],
                                      cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as child:
                    try:
                        status = Path(f'/proc/{child.pid}/status')
                        deadline = time.monotonic() + 2
                        while child.poll() is None and time.monotonic() < deadline:
                            mask = next(line for line in status.read_text().splitlines() if line.startswith('SigCgt:'))
                            if int(mask.split()[1], 16) & (1 << (signal.SIGTERM - 1)):
                                break
                            time.sleep(0.001)
                        else:
                            raise AssertionError('Meter review did not reach installed stop handling')
                        child.terminate()
                        out, err = child.communicate(timeout=3)
                        assert child.returncode == code and not out
                        assert err == expected_error
                    finally:
                        if child.poll() is None:
                            child.kill()
                            child.wait(timeout=3)

            assert not (root / 'stopped.cmr').exists() and not (root / 'stopped.cmr.tmp').exists()
            assert archive.read_bytes() == original_archive
            assert before == (tariff.read_bytes(), intervals.read_bytes())
            checks += 1

    print(f'meter-bill CLI: {checks} cases, resets, coverage, rate changes, read-only inputs and redacted errors')


if __name__ == '__main__':
    main()
