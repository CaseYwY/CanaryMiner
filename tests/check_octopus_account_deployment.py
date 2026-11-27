"""Run the installed authenticated capture through the installed billing watcher."""
from decimal import Decimal
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest import mock

from check_billing_watch_deployment import execute, stage, wait_until
from test_meter_bill_named_cli import SCOPE, utc
from test_octopus_cycle_review_cli import cycle
from test_octopus_meter_rates_cli import GO_PRODUCT


def main(installed):
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
        if output_flag != '--next-cycle':
            arguments += ['--scope', SCOPE, '--begin', capture.iso_utc(start), '--end', capture.iso_utc(finish)]
        with (mock.patch.object(capture, 'read_identity', return_value=IDENTITY),
              mock.patch.object(capture, 'private_opener', return_value=provider),
              mock.patch.object(sys, 'argv', arguments)):
            capture.main()
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
        target = workspace / f'cycle-{may}.cmr'
        stage(workspace / f'cycle-{may}.rates.json', following[2])
        watch = [miner, 'meter-bill', '--roll-forward', seed, '--watch-every-seconds', '1', '--watch-quiet']
        with subprocess.Popen(watch, cwd=workspace, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE) as child:
            try:
                wait_until((workspace / 'meter-billing.lock').exists)
                assert not target.exists()
                collect(april, may, '--next-cycle', seed)
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
    print('Installed account capture produced the readings bundle and the installed watcher closed and replayed its cycle.')


if __name__ == '__main__':
    main(Path(sys.argv[1]).resolve(strict=True))
