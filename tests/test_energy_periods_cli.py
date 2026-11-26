"""The installed energy CLI reads one audited source and selects civil periods."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

from test_meter_bill_named_cli import BUNDLE, named, utc


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
                       (str(root), file.name, 'fixture.local-session', 'channel.', 'selection.', 'energy-persistence')), result
            counts += 1
            return result.stdout

        def report(directory=state, at=through, cycles=2, **kwargs):
            return run('--state-dir', directory, '--through-utc-ms', at,
                       '--reset-policy', file, '--cycles', cycles, **kwargs)

        def seed(directory, shape):
            subprocess.run([fixture, directory, str(boundary), shape], cwd=root, check=True, timeout=20)
            return next(directory.glob('energy-history.*.db'))

        def snapshot(directory):
            return {p.name: p.read_bytes() for p in directory.iterdir()
                    if p.name.endswith(('.json', '.db'))}

        help_text = run('--help', binary=ordinary)
        assert '--through-utc-ms' in help_text and '--reset-policy' in help_text
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
        for invalid in ('{"monthly_day":1,"monthly_day":2,"clock":{}}', '{"monthly_day":31}',
                        '{"monthly_day":0,"clock":{}}', '{"monthly_day":1,"clock":{},"unknown":false}'):
            file.write_text(invalid, encoding='ascii')
            report(code=3)
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

        for shape in ('prepared', 'committed'):
            destination = root / shape
            seed(destination, shape)
            original = snapshot(destination)
            report(destination, code=3, message='require recovery review')
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

        if tzdb:
            policy(clock={'kind': 'utc', 'fixed_offset_seconds': 0})
            result = report(destination, at=utc('2024-04-01T00:00:02'))
            assert 'exact 1200000 W-ms' in result and result.count(': unavailable') == 1
            policy()
            (installed / BUNDLE / 'version').write_text('wrong', encoding='ascii')
            report(destination, code=3, message='monthly reset periods unavailable')
        else:
            policy(day=31, clock={'kind': 'fixed_standard', 'fixed_offset_seconds': -18000})
            result = report(destination, at=utc('2024-02-29T05:00:01'), cycles=2)
            february = utc('2024-02-29T05:00:00')
            assert f'Period [{utc("2024-01-31T05:00:00")}, {february})' in result
            assert f'Period [{february}, {utc("2024-03-31T05:00:00")})' in result

        print(f'energy periods: {counts} command invocations, one committed snapshot per report')


if __name__ == '__main__':
    main(*(Path(p).resolve(strict=True) for p in sys.argv[1:]))
