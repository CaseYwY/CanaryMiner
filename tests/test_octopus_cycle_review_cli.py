"""Close adjacent Octopus billing cycles using separate captures and private originals."""
import copy
import ctypes
from decimal import Decimal
from hashlib import sha256
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time

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

        def run(*args, code=0, reason='', cwd=root):
            nonlocal count
            result = subprocess.run([binary, 'meter-bill', *map(str, args)], cwd=cwd, capture_output=True,
                                    text=True, timeout=20)
            assert result.returncode == code, (args, result)
            private = (tariff.name, rates.name, readings.name, invoice.name, 'FORBIDDEN_DEVICE_SETUP')
            if code or args[0] not in ('--next-readings-plan', '--scheduled-readings-plan'):
                private += (SCOPE,)
            assert all(value not in result.stdout + result.stderr for value in private)
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

        def prepare(previous, output='generated-tariff.json', *, kind='--save-tariff', **expectation):
            return run('--prepare-next-cycle', previous, kind, output, **expectation)

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

        prepare('partial.cmr', 'rejected.json', code=3, reason='previous review must be a complete')
        assert not (root / 'rejected.json').exists() and not (root / 'rejected.json.tmp').exists()
        for flags in (('--prepare-next-cycle', old), ('--save-tariff', 'x.json'),
                      ('--prepare-next-cycle', old, '--save-review', 'x.cmr'),
                      ('--prepare-next-cycle', old, '--save-register', 'x.cmr'),
                      ('--prepare-next-cycle', old, '--save-tariff', '../x.json'),
                      ('--prepare-next-cycle', old, '--save-register', 'x.json', '--save-tariff', 'y.json')):
            run(*flags, code=2, reason='invalid options')
        prepared = prepare(old)
        assert f'[{april}, {may})' in prepared and f'Payment method {PAYMENT}' in prepared and '42.29' not in prepared
        starter_report = prepare(old, 'opening-register.json', kind='--save-register')
        assert '42.29' not in starter_report and 'Saved the opening register' in starter_report
        tariff_draft = root / 'generated-tariff.json'
        saved_tariff = tariff_draft.read_bytes()
        draft = json.loads(saved_tariff)
        assert draft['confirmed_utc'] == previous[0]['confirmed_utc']
        assert draft['reset_policy'] == previous[0]['reset_policy'] and draft['plan']['clock'] == LONDON
        assert draft['plan']['key'] != previous[0]['plan']['key']
        assert draft['plan']['revisions'][0]['dynamic'] == []
        assert draft['plan']['revisions'][0]['begin_utc_ms'] == april
        assert draft['plan']['revisions'][0]['end_utc_ms'] == may
        assert draft['opening']['through_utc_ms'] == draft['opening']['cycle_begin_utc_ms'] == april
        assert draft['opening']['cycle_end_utc_ms'] == may
        assert draft['opening']['imported_kwh'] == draft['opening']['accrued_subtotal'] == '0'
        starter = json.loads((root / 'opening-register.json').read_bytes())
        assert starter == register([(april, '42.29')])
        prepare(old, code=3, reason='existing file was preserved')
        assert tariff_draft.read_bytes() == saved_tariff and not tariff_draft.with_suffix('.json.tmp').exists()
        assert old.read_bytes() == old_bytes

        inputs(current)
        refused('partial.cmr', may + HOUR, 'previous review must be a complete')
        refused(old, april + HOUR - 1, 'precedes the saved cutoff')
        refused(old, may + HOUR - 1, 'rates observed after')
        next_review = ('--next-cycle-review', old, '--intervals', readings, '--save-review', 'x.cmr')
        for flags in (next_review, (*next_review, '--tariff', tariff),
                      (*next_review, '--allow-public-rates', '--allow-public-rates'),
                      (*next_review, '--allow-public-rates', '--tariff', tariff),
                      (*next_review, '--allow-public-rates', '--octopus-rates', rates),
                      (*next_review, '--allow-public-rates', '--as-of-utc-ms', may + HOUR),
                      (*next_review, '--allow-public-rates', '--dns-server', 'invalid'),
                      (*next_review, '--octopus-rates', rates, '--dns-server', '1.1.1.1'),
                      ('--previous-cycle', old, '--tariff', tariff, '--intervals', readings)):
            run(*flags, code=2, reason='invalid options')
        assert not (root / 'x.cmr').exists() and not (root / 'x.cmr.tmp').exists()
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
        tariff.write_bytes(saved_tariff)
        starter['register_readings'].extend(current[1]['register_readings'][1:])
        write(readings, starter)
        source_bytes = [path.read_bytes() for path in (tariff, readings, rates)]
        write(invoice, dict(schema_version=1, scope=SCOPE, currency='GBP', decimal_places=2, rounding='half_even',
                            cycles=[dict(begin_utc_ms=april, end_utc_ms=may, supply_charge=str(current[3]))]))
        report = advance(old, 'april.cmr', may + HOUR, '--invoice', invoice)
        derived = run('--next-cycle-review', old, '--intervals', readings, '--octopus-rates', rates,
                      '--as-of-utc-ms', may + HOUR, '--invoice', invoice, '--save-review', 'april-derived.cmr')
        assert derived == report
        assert (root / 'april-derived.cmr').read_bytes() == (root / 'april.cmr').read_bytes()
        assert run('--open-review', 'april-derived.cmr', '--previous-cycle', old) == derived
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
        prepare('tampered.cmr', 'tampered.json', code=3, reason='payment method, meter or reset rule differs')
        assert not (root / 'tampered.json').exists()
        for path in (old, tariff, readings, rates, invoice):
            path.unlink()
        assert run('--open-review', archived) == report
        run('--open-review', archived, '--previous-cycle', old, code=3, reason='archive unavailable')

        inputs(following)
        assert f'[{may}, {june})' in prepare(archived, 'may-tariff.json')
        tariff.write_bytes((root / 'may-tariff.json').read_bytes())
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
        assert f'[{november}, {december})' in prepare('go-october.cmr', 'go-next.json')
        prepare('go-october.cmr', 'go-opening.json', kind='--save-register', code=3, reason='no absolute closing')
        assert not (root / 'go-opening.json').exists()
        tariff.write_bytes((root / 'go-next.json').read_bytes())
        go = advance('go-october.cmr', 'go-november.cmr', december + HOUR)
        close(go_new[3], go)
        assert 'Octopus Go public rate snapshot' in go and 'API quantities are reported' in go
        assert run('--open-review', 'go-november.cmr', '--previous-cycle', 'go-october.cmr') == go
        planner = root / 'readings planner'
        planner.mkdir()
        planner_seed = Path(shutil.copy2(root / 'go-october.cmr', planner))
        query = ('--next-readings-plan', planner_seed)
        expected_plan = dict(schema_version=1, scope=SCOPE, begin_utc_ms=november, end_utc_ms=december,
                             predecessor_sha256=sha256(planner_seed.read_bytes()).hexdigest())
        assert json.loads(run(*query, cwd=planner)) == expected_plan
        scheduled = ('--scheduled-readings-plan', planner_seed)
        seed_digest = expected_plan['predecessor_sha256']
        assert json.loads(run(*scheduled, cwd=planner)) == dict(expected_plan, seed_sha256=seed_digest, ready=True)
        publishing = planner / f'cycle-{december}.cmr.tmp'
        publishing.write_bytes(b'watcher is staging the next archive')
        assert json.loads(run(*scheduled, cwd=planner)) == dict(expected_plan, seed_sha256=seed_digest, ready=False)
        run(*query, cwd=planner, code=3, reason='uncertain temporary output')
        publishing.unlink()
        assert set(planner.iterdir()) == {planner_seed}
        run(*query, code=3, reason='seed archive\'s workspace')
        for extra in (('--save-review', 'new.cmr'), ('--allow-public-rates',), ('--as-of-utc-ms', november),
                      ('--roll-forward', planner_seed)):
            run(*query, *extra, cwd=planner, code=2, reason='invalid options')
        for suffix in ('.readings.json', '.pages'):
            existing = planner / f'cycle-{december}{suffix}'
            existing.write_bytes(b'staged input')
            run(*query, cwd=planner, code=3, reason='readings are already staged')
            assert existing.read_bytes() == b'staged input'
            existing.unlink()
        pages_slot = planner / f'cycle-{december}.pages'
        pages_slot.mkdir()
        assert json.loads(run(*scheduled, cwd=planner)) == dict(expected_plan, seed_sha256=seed_digest, ready=False)
        pages_slot.rmdir()
        successor = planner / f'cycle-{december}.cmr'
        shutil.copy2(root / 'go-november.cmr', successor)
        expected_plan.update(begin_utc_ms=december, end_utc_ms=utc('2024-01-01T00:00:00'),
                             predecessor_sha256=sha256(successor.read_bytes()).hexdigest())
        assert json.loads(run(*query, cwd=planner)) == expected_plan
        assert json.loads(run(*scheduled, cwd=planner)) == dict(expected_plan, seed_sha256=seed_digest, ready=True)
        damaged = successor.read_bytes()
        successor.write_bytes(damaged[:-1])
        run(*query, cwd=planner, code=3, reason='successor archive is invalid')
        successor.unlink()
        gap = planner / f"cycle-{expected_plan['end_utc_ms']}.cmr"
        gap.write_bytes(damaged)
        run(*query, cwd=planner, code=3, reason='archive gap')
        run('--next-readings-plan', archived, code=3, reason='requires an eligible Octopus half-hour import cycle')
        supplier_workspace = root / 'supplier pages'
        workspace_pages = supplier_workspace / f'cycle-{december}.pages'
        workspace_pages.mkdir(parents=True)
        write(supplier_workspace / f'cycle-{december}.rates.json', go_new[2])
        rows = list(reversed(go_new[1]['consumption']))
        link = ('https://api.oegb-kraken.energy/v1/electricity-meter-points/0000000000000'
                '/meters/fixture-supplier-reading/consumption/?envelope=redacted-link')
        for pos in range(0, len(rows), 100):
            write(workspace_pages / f'page-{pos // 100 + 1}.json',
                  dict(count=len(rows), previous=link if pos else None,
                       next=link if pos + 100 < len(rows) else None, results=rows[pos:pos + 100]))
        seed = root / 'go-october.cmr'
        supplier_roll = ('--roll-forward', seed, '--as-of-utc-ms', december + HOUR)
        staged = supplier_workspace / f'cycle-{december}.cmr'
        assert 'waiting for complete meter readings' in run(*supplier_roll, cwd=supplier_workspace)
        assert 'waiting for complete meter readings' in run(*supplier_roll, '--confirm-electricity-import',
                                                             cwd=supplier_workspace)
        assert not staged.exists()
        write(workspace_pages / 'observation.json', {'observed_utc_ms': december + HOUR})
        run('--roll-forward', seed, '--as-of-utc-ms', december + HOUR - 1, '--confirm-electricity-import',
            cwd=supplier_workspace, code=3, reason='page observation is invalid')
        assert not staged.exists()
        register_workspace = root / 'register pages'
        (register_workspace / f'cycle-{may}.pages').mkdir(parents=True)
        # A register-based seed has an absolute opening control and cannot adopt supplier quantities.
        old.write_bytes(old_bytes)
        run('--roll-forward', old, '--as-of-utc-ms', may + HOUR, '--confirm-electricity-import',
            cwd=register_workspace, code=3, reason='cannot continue a cumulative-register archive')
        old.unlink()
        live_stage = run('--confirm-electricity-import', *supplier_roll, cwd=supplier_workspace)
        assert '0 verified, 1 saved' in live_stage
        assert not (supplier_workspace / f'cycle-{december}.readings.json').exists()
        assert b'fixture-supplier-reading' not in staged.read_bytes() and b'redacted-link' not in staged.read_bytes()
        shutil.rmtree(workspace_pages)
        assert '1 verified, 0 saved' in run(*supplier_roll, cwd=supplier_workspace)
        assert run('--open-review', staged, '--previous-cycle', seed) == go
        inputs(cycle(november, december, GO_PRODUCT, Decimal(0), 20, 5, 30))
        refused('go-october.cmr', december + HOUR, 'payment method, meter or reset rule differs')
        # Both Octopus captures already include VAT. Each cycle may annotate that included tax.
        def vat(cycle, rate, *, added=False):
            result = copy.deepcopy(cycle)
            declaration = result[0]
            opening = declaration['opening']
            begin, end = opening['cycle_begin_utc_ms'], opening['cycle_end_utc_ms']
            declaration['schema_version'] = 15
            declaration['plan']['tax_periods'] = [dict(begin_utc_ms=begin, end_utc_ms=end, published_utc_ms=begin,
                rules=[dict(key='vat', kind='fraction', rate=rate, components=['energy'] if added else [],
                            prior_taxes=[], included_in=['daily_fee'] if added else ['energy', 'daily_fee'])])]
            opening['tax_bases'] = dict.fromkeys(('energy', 'daily_fee', 'cycle_fee', 'demand', 'billed_kwh'), '0')
            return result

        inputs(vat(previous, '0.05'))
        vat_first = save('vat-march.cmr', april + HOUR)
        close(previous[3], vat_first)
        assert 'Review any included-price tax annotations' in prepare('vat-march.cmr', 'vat-next.json')
        vat_draft = json.loads((root / 'vat-next.json').read_bytes())
        assert vat_draft['schema_version'] == 4 and 'tax_periods' not in vat_draft['plan']
        assert 'tax_bases' not in vat_draft['opening']
        inputs(current)
        tariff.write_bytes((root / 'vat-next.json').read_bytes())
        close(current[3], advance('vat-march.cmr', 'vat-with-generated.cmr', may + HOUR))
        inputs(vat(current, '0.2'))
        vat_next = advance('vat-march.cmr', 'vat-april.cmr', may + HOUR)
        close(current[3], vat_next)
        assert run('--open-review', 'vat-april.cmr', '--previous-cycle', 'vat-march.cmr') == vat_next
        assert run('--open-review', 'vat-march.cmr') == vat_first
        inputs(vat(current, '0.2', added=True))
        refused('vat-march.cmr', may + HOUR, 'already include VAT')
        annual_begin = utc('2023-01-01T00:00:00')
        annual_end = utc('2024-01-01T00:00:00')
        annual_next = utc('2025-01-01T00:00:00')
        annual = cycle(annual_begin, annual_end, GO_PRODUCT, Decimal(100), 14, 6, 365)
        annual[0].update(schema_version=5, reset_policy=dict(annual_month=1, annual_day=1, clock=LONDON))
        inputs(annual)
        close(annual[3], save('annual.cmr', annual_end + HOUR))
        annual_saved = prepare('annual.cmr', 'annual-next.json')
        assert f'[{annual_end}, {annual_next})' in annual_saved and 'exceeds the installed public rate capture bounds' in annual_saved

        leap = cycle(annual_end, annual_next, GO_PRODUCT, None, 14, 6, 366, bundle=True)
        leap[0].update(schema_version=5, reset_policy=dict(annual_month=1, annual_day=1, clock=LONDON))
        leap_pages = root / 'leap year pages'
        leap_pages.mkdir()
        summer_start = utc('2024-03-31T01:00:00')
        summer_end = utc('2024-10-27T01:00:00')

        def supplier_time(at):
            if summer_start <= at < summer_end:
                return timestamp(at + HOUR).replace('Z', '+01:00')
            return timestamp(at)

        leap_rows = [dict(consumption=0.015, interval_start=supplier_time(at), interval_end=supplier_time(at + HALF_HOUR))
                     for at in range(annual_end, annual_next, HALF_HOUR)]
        assert len(leap_rows) == 17568
        assert len(json.dumps(leap_rows, separators=(',', ':')).encode('ascii')) > 1792 * 1024
        for pos in range(0, len(leap_rows), 100):
            write(leap_pages / f'page-{pos // 100 + 1}.json',
                  dict(count=len(leap_rows), previous=link if pos else None,
                       next=link if pos + 100 < len(leap_rows) else None, results=leap_rows[pos:pos + 100]))
        write(leap_pages / 'observation.json', {'observed_utc_ms': annual_next + HOUR})
        write(tariff, leap[0])
        write(rates, leap[2])
        run('--import-octopus-pages', leap_pages, '--tariff', tariff, '--save-readings', 'leap-import.json',
            '--confirm-electricity-import')
        readings.write_bytes((root / 'leap-import.json').read_bytes())
        assert readings.stat().st_size <= 1792 * 1024 and b'+01:00' not in readings.read_bytes()
        leap_report = save('leap.cmr', annual_next + HOUR)
        close(leap[3], leap_report)
        assert run('--open-review', 'leap.cmr') == leap_report

        old.write_bytes(old_bytes)
        roll = ('--roll-forward', old, '--as-of-utc-ms', june + HOUR)
        first_stem = root / f'cycle-{may}'
        second_stem = root / f'cycle-{june}'
        first_archive = first_stem.with_suffix('.cmr')
        second_archive = second_stem.with_suffix('.cmr')
        for extras in (('--intervals', readings), ('--tariff', tariff), ('--next-cycle-review', old),
                       ('--save-review', 'x.cmr'), ('--allow-public-rates',), ('--dns-server', '1.1.1.1')):
            run(*roll, *extras, code=2, reason='invalid options')
        run('--roll-forward', 'partial.cmr', code=3, reason='previous review must be a complete')
        run('--roll-forward', old, '--as-of-utc-ms', april, code=3, reason='precedes the saved cutoff')
        write(second_stem.with_suffix('.rates.json'), following[2])
        assert 'waiting for complete meter readings' in run(*roll)
        assert not first_archive.exists() and not second_archive.exists()
        write(first_stem.with_suffix('.readings.json'), current[1])
        assert 'waiting for a saved Octopus rate capture' in run(*roll)
        assert 'waiting for the cycle to close' in run('--roll-forward', old, '--as-of-utc-ms', may - 1)

        write(first_stem.with_suffix('.rates.json'), current[2])
        write(first_stem.with_suffix('.invoice.json'), dict(schema_version=1, scope=SCOPE, currency='GBP',
            decimal_places=2, rounding='half_even',
            cycles=[dict(begin_utc_ms=april, end_utc_ms=may, supply_charge=str(current[3]))]))
        bad_readings = copy.deepcopy(following[1])
        bad_readings['register_readings'][0]['imported_kwh'] = '0'
        write(second_stem.with_suffix('.readings.json'), bad_readings)
        run(*roll, code=3, reason=f'cycle closing {june} UTC ms\nmeter-bill: this run committed 1 new cycle\n'
                                 'meter-bill: the new opening register differs')
        assert first_archive.exists() and not second_archive.exists()
        assert not second_stem.with_suffix('.cmr.tmp').exists()
        first_bytes = first_archive.read_bytes()
        first_report = run('--open-review', first_archive, '--previous-cycle', old)
        close(current[3], first_report)
        assert '(match)' in first_report

        write(second_stem.with_suffix('.readings.json'), following[1])
        resumed = run(*roll)
        assert '1 verified, 1 saved' in resumed and f'Last verified closing {june}' in resumed
        assert first_archive.read_bytes() == first_bytes
        close(following[3], run('--open-review', second_archive, '--previous-cycle', first_archive))
        second_bytes = second_archive.read_bytes()
        first_stem.with_suffix('.readings.json').write_bytes(b'changed after publication')
        assert '2 verified, 0 saved' in run(*roll)
        assert first_archive.read_bytes() == first_bytes and second_archive.read_bytes() == second_bytes
        run('--roll-forward', old, '--as-of-utc-ms', may + HOUR, code=3, reason='newer than the requested cutoff')

        write(first_stem.with_suffix('.readings.json'), current[1])
        first_archive.unlink()
        run(*roll, code=3, reason='archive gap')
        assert not first_archive.exists() and second_archive.read_bytes() == second_bytes
        first_archive.write_bytes(first_bytes)

        second_archive.write_bytes(first_bytes)
        run(*roll, code=3, reason='does not match the previous closing record')
        assert second_archive.read_bytes() == first_bytes
        second_archive.unlink()
        uncertain = second_stem.with_suffix('.cmr.tmp')
        uncertain.write_bytes(second_bytes)
        run(*roll, code=3, reason='uncertain temporary output')
        assert not second_archive.exists() and uncertain.read_bytes() == second_bytes
        uncertain.unlink()
        second_archive.write_bytes(second_bytes)
        assert '2 verified, 0 saved' in run(*roll)

        for extra in (('0',), ('01',), ('86401',), ('1.0',), ('1', '--watch-every-seconds', '1'),
                      ('1', '--as-of-utc-ms', june + HOUR)):
            run('--roll-forward', old, '--watch-every-seconds', *extra, code=2, reason='invalid options')
        run('--open-review', old, '--watch-every-seconds', '1', code=2, reason='invalid options')
        run('--roll-forward', old, '--watch-quiet', code=2, reason='invalid options')
        run('--roll-forward', old, '--watch-every-seconds', '1', '--watch-quiet', '--watch-quiet',
            code=2, reason='invalid options')

        watched = root / 'watched workspace'
        watched.mkdir()
        watched_seed = watched / 'seed.cmr'
        watched_seed.write_bytes(old_bytes)
        watch = (binary, 'meter-bill', '--roll-forward', watched_seed, '--watch-every-seconds', '1')
        first_target, second_target = (watched / f'cycle-{at}.cmr' for at in (may, june))
        can_signal = os.name != 'nt' or ctypes.windll.kernel32.GetConsoleCP() != 0
        flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == 'nt' and can_signal else 0

        def publish_for_watch(at, kind, value):
            destination = watched / f'cycle-{at}.{kind}.json'
            staging = destination.with_suffix('.staged')
            write(staging, value)
            staging.replace(destination)

        def await_watch(child, output, fragment):
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                if fragment in output.read_text(encoding='ascii'):
                    return
                assert child.poll() is None, (fragment, child.returncode, output.read_text(encoding='ascii'))
                time.sleep(0.025)
            raise AssertionError((fragment, output.read_text(encoding='ascii')))

        output, errors = watched / 'watch.log', watched / 'watch-errors.log'
        with output.open('wb') as log, errors.open('wb') as err, subprocess.Popen(
                watch, cwd=watched, stdout=log, stderr=err, creationflags=flags) as child:
            try:
                await_watch(child, output, 'waiting for complete meter readings')
                time.sleep(1.25)
                assert output.read_text(encoding='ascii').count('waiting for complete meter readings') == 1
                run('--roll-forward', watched_seed, cwd=watched, code=3, reason='workspace is busy')
                if os.name != 'nt':
                    (watched / 'meter-billing.lock').unlink()
                    run('--roll-forward', watched_seed, cwd=watched, code=3, reason='workspace is busy')
                publish_for_watch(may, 'readings', current[1])
                await_watch(child, output, 'waiting for a saved Octopus rate capture')
                assert not first_target.exists()
                fresh = dict(current[2], observed_utc_ms=time.time_ns() // 1_000_000)
                publish_for_watch(may, 'rates', fresh)
                await_watch(child, output, '0 verified, 1 saved')
                close(current[3], run('--open-review', first_target, '--previous-cycle', watched_seed, cwd=watched))
                publish_for_watch(june, 'readings', following[1])
                publish_for_watch(june, 'rates', following[2])
                await_watch(child, output, '1 verified, 1 saved')
                close(following[3], run('--open-review', second_target, '--previous-cycle', first_target, cwd=watched))
                held = watched / 'held-latest.cmr'
                second_target.rename(held)
                assert child.wait(timeout=10) == 3
                assert 'archive changed or disappeared while watching' in errors.read_text(encoding='ascii')
                assert not second_target.exists() and not second_target.with_suffix('.cmr.tmp').exists()
                held.rename(second_target)
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait(timeout=5)

        if can_signal:
            with output.open('wb') as log, errors.open('wb') as err, subprocess.Popen(
                    (*watch[:-1], '300'), cwd=watched, stdout=log, stderr=err, creationflags=flags) as child:
                try:
                    await_watch(child, output, '2 verified, 0 saved')
                    child.send_signal(signal.CTRL_BREAK_EVENT if os.name == 'nt' else signal.SIGTERM)
                    assert child.wait(timeout=5) == 130
                    assert 'recurring review stopped' in errors.read_text(encoding='ascii')
                finally:
                    if child.poll() is None:
                        child.kill()
                        child.wait(timeout=5)
        if os.name != 'nt':
            moved = root / 'moved workspace'
            with output.open('wb') as log, errors.open('wb') as err, subprocess.Popen(
                    watch, cwd=watched, stdout=log, stderr=err) as child:
                try:
                    await_watch(child, output, '2 verified, 0 saved')
                    watched.rename(moved)
                    assert child.wait(timeout=5) == 3
                    assert 'workspace moved' in (moved / errors.name).read_text(encoding='ascii')
                finally:
                    if child.poll() is None:
                        child.kill()
                        child.wait(timeout=5)
                    if moved.exists():
                        moved.rename(watched)
        assert '2 verified, 0 saved' in run('--roll-forward', watched_seed, cwd=watched)
        # A supervisor can launch the quiet watch without a reader on stdout.
        lease = watched / 'meter-billing.lock'
        lease.unlink()
        with subprocess.Popen((*watch, '--watch-quiet'), cwd=watched, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE) as child:
            child.stdout.close()
            try:
                deadline = time.monotonic() + 10
                while not lease.exists() and time.monotonic() < deadline:
                    assert child.poll() is None
                    time.sleep(0.05)
                assert lease.exists(), 'quiet watcher never acquired the workspace'
                run('--roll-forward', watched_seed, cwd=watched, code=3, reason='workspace is busy')
                watched_seed.write_bytes(b'damaged archive')
                assert child.wait(timeout=10) == 3
                assert b'recurring review seed' in child.stderr.read()
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait(timeout=5)
                watched_seed.write_bytes(old_bytes)
        assert '2 verified, 0 saved' in run('--roll-forward', watched_seed, cwd=watched)

        feb = utc('2023-02-01T00:00:00')
        drift = cycle(feb, march, GO_PRODUCT, Decimal(100), 14, 6, 28)
        drift[0]['reset_policy']['clock'] = dict(kind='utc', fixed_offset_seconds=0)
        inputs(drift)
        close(drift[3], save('utc-reset.cmr', march + HOUR))
        prepare('utc-reset.cmr', 'drift.json', code=3, reason='London local midnight')
        assert not (root / 'drift.json').exists()
        print(f'Octopus adjacent cycle review and preparation passed {count} installed CLI commands.')


if __name__ == '__main__':
    main(*(Path(path).resolve(strict=True) for path in sys.argv[1:]))
