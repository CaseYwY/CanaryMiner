"""Run the installed authenticated capture through the installed billing watcher."""
from decimal import Decimal
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest import mock

from check_billing_watch_deployment import ScheduledTask, UserUnit, execute, stage, wait_until
from test_meter_bill_named_cli import SCOPE, utc
from test_octopus_cycle_review_cli import cycle
from test_octopus_meter_rates_cli import GO_PRODUCT


def main(installed, *, scheduled=False, service=False):
    tools = installed / 'tools'
    with mock.patch.object(sys, 'path', [str(tools), *sys.path]):
        from test_capture_octopus_consumption import IDENTITY, PROVIDER, Provider, account, page_documents, capture

    assert Path(capture.__file__).parent == tools
    miner = installed / 'bin' / ('canary-miner.exe' if os.name == 'nt' else 'canary-miner')
    execute(sys.executable, '-I', '-B', tools / 'capture_octopus_consumption.py', '--help')
    march, april, may = map(utc, ('2023-03-01T00:00:00', '2023-03-31T23:00:00', '2023-04-30T23:00:00'))

    def collect(begin, end, output_flag, output):
        start, finish = (capture.EPOCH + capture.MILLISECOND * instant for instant in (begin, end))
        provider = Provider(account(), *page_documents(start, finish))
        arguments = ['capture_octopus_consumption.py', '--confirm-electricity-import', output_flag, str(output)]
        if output_flag not in ('--next-cycle', '--run-scheduled'):
            arguments += ['--scope', SCOPE, '--begin', capture.iso_utc(start), '--end', capture.iso_utc(finish)]
        with (mock.patch.object(capture, 'read_identity', return_value=IDENTITY) as prompt,
              mock.patch.object(capture, 'private_opener', return_value=provider),
              mock.patch.object(sys, 'argv', arguments)):
            capture.main()
        assert prompt.call_count == (0 if output_flag == '--run-scheduled' else 1)
        assert len(provider.requests) == 16
        assert all(req.full_url.startswith(PROVIDER + '/v1/') for req in provider.requests)

    with tempfile.TemporaryDirectory(prefix='canary-account-watch-') as directory:
        workspace = Path(directory)
        prior = cycle(march, april, GO_PRODUCT, Decimal(0), 12, 34, 31, bundle=True)
        following = cycle(april, may, GO_PRODUCT, Decimal(0), 15, 40, 30, bundle=True)
        tariff, rates, initial = (workspace / name for name in ('tariff.json', 'rates.json', 'initial.json'))
        stage(tariff, prior[0])
        stage(rates, prior[2])
        collect(march, april, '--output', initial)
        seed = workspace / 'march.cmr'
        original = execute(miner, 'meter-bill', '--tariff', tariff, '--intervals', initial, '--octopus-rates', rates,
                           '--save-review', seed.name, cwd=workspace)
        assert f'Cycle estimate at closing {prior[3]:.6f} GBP'.encode('ascii') in original
        seed = seed.rename(workspace / '-march-\u7535.cmr')
        if scheduled:
            sealed = workspace / capture.CREDENTIAL_NAME
            source = Provider(account())
            flags = ['capture_octopus_consumption.py', '--confirm-electricity-import', '--enroll-scheduled', str(seed)]
            with (mock.patch.object(capture, 'read_identity', return_value=IDENTITY),
                  mock.patch.object(capture, 'private_opener', return_value=source),
                  mock.patch.object(sys, 'argv', flags)):
                capture.main()
            assert len(source.requests) == 1, source.requests
            assert all(value.encode('ascii') not in sealed.read_bytes() for value in IDENTITY)
        target = workspace / f'cycle-{may}.cmr'
        stage(workspace / f'cycle-{may}.rates.json', following[2])
        watch = [miner, 'meter-bill', '--roll-forward', seed, '--watch-every-seconds', '1', '--watch-quiet']
        with subprocess.Popen(watch, cwd=workspace, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE) as child:
            try:
                wait_until((workspace / 'meter-billing.lock').exists)
                assert not target.exists()
                collect(april, may, '--run-scheduled' if scheduled else '--next-cycle', seed)
                wait_until(target.exists)
                assert child.poll() is None
            finally:
                if child.poll() is None:
                    child.terminate()
                child.wait(timeout=10)
        report = execute(miner, 'meter-bill', '--open-review', target, '--previous-cycle', seed, cwd=workspace)
        assert f'Cycle estimate at closing {following[3]:.6f} GBP'.encode('ascii') in report, report
        saved = target.read_bytes()
        assert all(secret.encode('ascii') not in saved for secret in IDENTITY)
        (workspace / f'cycle-{may}.readings.json').unlink()
        assert b'1 verified, 0 saved' in execute(miner, 'meter-bill', '--roll-forward', seed, cwd=workspace)
        assert target.read_bytes() == saved
        if scheduled:
            next_end = utc('2023-05-31T23:00:00')
            (workspace / f'cycle-{next_end}.pages').mkdir()
            assert b'"ready":false' in execute(miner, 'meter-bill', '--scheduled-readings-plan', seed, cwd=workspace)
            entry = (sys.executable, '-I', '-B', tools / 'capture_octopus_consumption.py',
                     '--confirm-electricity-import', '--run-scheduled', seed)
            assert not execute(*entry, cwd=workspace)
            if service:
                python = Path(sys.executable)
                if os.name == 'nt':
                    python = python.with_name('pythonw.exe')
                manager = ScheduledTask if os.name == 'nt' else UserUnit
                lease = workspace / capture.CAPTURE_LEASE_NAME
                lease.unlink()
                label = 'capture-' + Path(directory).name.rsplit('-', 1)[-1].replace('_', 'x')
                deployed = manager(installed, seed, label, capture_python=python)
                try:
                    deployed.start()
                    wait_until(lease.is_file)
                    execute(*entry, cwd=workspace, expected=1, reason=b'another scheduled capture owns')
                finally:
                    try:
                        deployed.stop()
                    finally:
                        deployed.remove()
                assert not execute(*entry, cwd=workspace)
    print('Installed account capture produced the readings bundle and the installed watcher closed and replayed its cycle.')
    if scheduled:
        print('Current-user credential enrollment, unattended capture and idle plan verified.')


if __name__ == '__main__':
    main(Path(sys.argv[1]).resolve(strict=True), scheduled='--scheduled' in sys.argv, service='--service' in sys.argv)
