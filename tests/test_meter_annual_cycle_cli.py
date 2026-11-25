"""Exercise annual tier settlement, reset boundaries and saved review continuation."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

from test_meter_bill_named_cli import BUNDLE, named, plan, register, utc

FIXED = {'kind': 'utc', 'fixed_offset_seconds': 0}
HALF_HOUR = 1_800_000


def declaration(begin, end, following, month, day, reset_clock):
    reset = {'annual_month': month, 'annual_day': day, 'clock': reset_clock}
    draft = plan(begin, begin, end, FIXED, reset, following)
    draft['schema_version'] = 5
    draft['plan'].update(tiers='progressive', cycle_charge='10')
    rate = draft['plan']['revisions'][0]
    rate.update(base_rate_per_kwh='0.1', bands=[
        {'through_kwh': '100', 'rate_per_kwh': '0'},
        {'through_kwh': None, 'rate_per_kwh': '0.2'},
    ])
    return draft


def main(cli, bundle=None):
    with TemporaryDirectory(prefix='canary-annual-meter-') as folder:
        root = Path(folder)
        if bundle:
            installed = root / 'bin \u03a9'
            installed.mkdir()
            cli = Path(shutil.copy2(cli, installed / cli.name))
            shutil.copytree(bundle, installed / BUNDLE)
        tariff, meter = root / 'annual tariff.json', root / 'annual meter.json'
        checks = 0

        def run(*args, code=0, message=None):
            nonlocal checks
            outcome = subprocess.run([cli, 'meter-bill', *map(str, args)], cwd=root,
                                     capture_output=True, text=True, timeout=30)
            assert outcome.returncode == code, outcome
            if code:
                assert not outcome.stdout and (message is None or message in outcome.stderr), outcome
            else:
                assert outcome.stdout and not outcome.stderr, outcome
            assert all(secret not in outcome.stdout + outcome.stderr for secret in
                       (tariff.name, meter.name, 'synthetic-private-meter')), outcome
            checks += 1
            return outcome.stdout

        def write(path, payload, **kwargs):
            path.write_text(json.dumps(payload, **kwargs), encoding='ascii')

        def review(cutoff, *args, **kwargs):
            return run('--tariff', tariff, '--intervals', meter, '--as-of-utc-ms', cutoff, *args, **kwargs)

        if bundle:
            begin = utc('2024-03-31T00:00:00')
            end = utc('2025-03-30T23:00:00')
            following = utc('2026-03-30T23:00:00')
            draft = declaration(begin, end, following, 3, 31, named('Europe/London'))
            write(tariff, draft)
            cutoff = end + HALF_HOUR
            write(meter, register([(begin, '8240'), (end, '8390'), (cutoff, '8391')]))
            report = review(cutoff, '--save-review', 'named.cmr')
            assert 'Cycle estimate at closing 35.000000 USD' in report
            assert 'Provisional cycle estimate 10.100000 USD' in report
            assert run('--open-review', 'named.cmr') == report
            draft['opening']['cycle_end_utc_ms'] += 3_600_000
            write(tariff, draft)
            review(cutoff, code=3, message='invalid')
            (installed / BUNDLE / 'version').write_text('wrong', encoding='ascii')
            run('--open-review', 'named.cmr', code=3, message='time database unavailable or invalid')
        else:
            begin = utc('2024-01-01T00:00:00')
            end = utc('2025-01-01T00:00:00')
            following = utc('2026-01-01T00:00:00')
            switch = utc('2024-07-01T00:00:00')
            draft = declaration(begin, end, following, 1, 1, FIXED)
            first_rate = draft['plan']['revisions'][0]
            first_rate['end_utc_ms'] = switch
            second_rate = dict(first_rate, key='second-reviewed-rate', begin_utc_ms=switch,
                               end_utc_ms=following, base_rate_per_kwh='0.2')
            draft['plan']['revisions'].append(second_rate)
            write(tariff, draft)
            steps = (end - begin) // HALF_HOUR
            at = lambda n: begin + n * HALF_HOUR
            suffixes = ('', '.25', '.5', '.75')
            points = [(at(i), str(1000 + i // 4) + suffixes[i % 4]) for i in range(steps + 1)]
            assert steps == 17568
            for label, wanted in [('2024-02-01T00:00:00', '101.600000'), ('2024-03-01T00:00:00', '206.000000')]:
                through = utc(label)
                n = (through - begin) // HALF_HOUR
                write(meter, register(points[:n + 1]))
                report = review(through)
                assert f'Provisional cycle estimate {wanted} USD' in report
                assert 'estimate at closing' not in report
            write(meter, register(points), indent=2)
            assert meter.stat().st_size > 1024 * 1024
            previous = review(end, '--save-review', 'closed.cmr')
            assert 'Cycle estimate at closing 1528.400000 USD' in previous
            prior = (root / 'closed.cmr').read_bytes()
            assert run('--open-review', 'closed.cmr') == previous
            ending = end + HALF_HOUR
            write(meter, register(points + [(ending, '5392.25')]), indent=2)
            report = run('--extend-review', 'closed.cmr', '--intervals', meter,
                         '--as-of-utc-ms', ending, '--save-review', 'extended.cmr')
            assert 'Cycle estimate at closing 1528.400000 USD' in report
            assert 'Provisional cycle estimate 10.050000 USD' in report
            assert (root / 'closed.cmr').read_bytes() == prior
            missing = points[:-1] + [(ending, '5392.25')]
            write(meter, register(missing))
            review(ending, code=3, message='needs a reading at the billing-cycle boundary')
            oversized = [(at(i), str(1000 + i)) for i in range(18_434)]
            write(meter, register(oversized))
            review(oversized[-1][0], code=3, message='invalid interval document')
            for schema in (3, 4):
                write(tariff, dict(draft, schema_version=schema))
                review(ending, code=3)
            write(tariff, dict(draft, reset_policy={'monthly_day': 1, 'clock': FIXED}))
            review(ending, code=3, message='invalid')
            tariff.unlink()
            meter.unlink()
            (root / 'closed.cmr').unlink()
            assert run('--open-review', 'extended.cmr') == report
        print(f'Annual tariff: {checks} ordinary meter-bill invocations passed')


if __name__ == '__main__':
    main(*(Path(value).resolve(strict=True) for value in sys.argv[1:]))
