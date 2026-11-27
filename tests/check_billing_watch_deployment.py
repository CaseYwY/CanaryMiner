"""Exercise an installed billing watcher with a disposable user's actual service manager."""
from decimal import Decimal
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

from test_meter_bill_named_cli import HOUR, utc
from test_octopus_cycle_review_cli import cycle
from test_octopus_meter_rates_cli import GO_PRODUCT


def execute(*args, cwd=None, env=None, expected=0):
    result = subprocess.run(args, cwd=cwd, env=env, capture_output=True, timeout=30)
    assert result.returncode == expected, result
    return result.stdout


def wait_until(probe):
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        if probe():
            return
        time.sleep(0.25)
    raise AssertionError(f'timed out waiting for {probe.__name__}')


def stage(path, value):
    pending = path.with_suffix('.staged')
    pending.write_text(json.dumps(value), encoding='ascii')
    pending.replace(path)


class ScheduledTask:
    def __init__(self, installed, seed, label):
        self.powershell = ('powershell.exe', '-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass')
        command = (*self.powershell, '-File', str(installed / 'deploy' / 'Register-BillingWatch.ps1'),
                   '-Name', label, '-Seed', str(seed), '-Seconds', '1')
        name = execute(*command).decode('ascii').strip()
        assert re.fullmatch(rf'CanaryMiner-Billing-{label}-S-1-[0-9-]+', name), name
        self.env = dict(os.environ, CANARY_BILLING_TEST_TASK=name)
        try:
            duplicate = subprocess.run(command, capture_output=True, timeout=30)
            assert duplicate.returncode != 0, duplicate
            xml = self.manage('Export-ScheduledTask').decode('ascii', errors='ignore')
            for property_value in ('LogonTrigger', 'InteractiveToken', 'LeastPrivilege', 'IgnoreNew', 'PT0S',
                                   '--watch-quiet'):
                assert property_value in xml, property_value
            assert '--allow-public-rates' not in xml and '--confirm-electricity-import' not in xml
        except BaseException:
            self.remove()
            raise

    def manage(self, command, suffix=''):
        return execute(*self.powershell, '-Command', "$ErrorActionPreference = 'Stop'; " +
                       command + ' -TaskName $env:CANARY_BILLING_TEST_TASK -TaskPath ' + "'\\'" + suffix,
                       env=self.env)

    def start(self):
        self.manage('Start-ScheduledTask')

    def state(self):
        return self.manage('(Get-ScheduledTask', ').State').strip()

    def stop(self):
        self.manage('Stop-ScheduledTask')
        wait_until(lambda: self.state() == b'Ready')

    def refused(self):
        return self.state() == b'Ready' and self.manage('(Get-ScheduledTaskInfo', ').LastTaskResult').strip() == b'3'

    def remove(self):
        self.manage('Unregister-ScheduledTask', ' -Confirm:$false')


class UserUnit:
    def __init__(self, installed, seed, label):
        self.name = f'canaryminer-billing-{label}.service'
        config = Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config'))
        self.unit = config / 'systemd' / 'user' / self.name
        self.unit.parent.mkdir(parents=True, exist_ok=True)
        renderer = installed / 'deploy' / 'billing-watch.sh'
        data = execute(renderer, seed, '1')
        assert b'--allow-public-rates' not in data and b'--confirm-electricity-import' not in data
        for option in ('--allow-public-rates', '--confirm-electricity-import'):
            assert option.encode('ascii') in execute(renderer, seed, '1', option)
            execute(renderer, seed, '1', option, option, expected=2)
        with self.unit.open('xb') as output:
            output.write(data)
        try:
            execute('systemd-analyze', '--user', 'verify', self.unit)
            execute('systemctl', '--user', 'daemon-reload')
            self.manage('enable')
            assert self.manage('is-enabled').strip() == b'enabled'
        except BaseException:
            self.remove()
            raise

    def manage(self, *args):
        return execute('systemctl', '--user', *args, self.name)

    def start(self):
        self.manage('start')

    def stop(self):
        previous = self.manage('show', '-P', 'ActiveState').strip()
        self.manage('stop')
        if previous == b'active':
            assert self.manage('show', '-P', 'Result').strip() == b'success'

    def refused(self):
        return (self.manage('show', '-P', 'ActiveState').strip() == b'failed' and
                self.manage('show', '-P', 'ExecMainStatus').strip() == b'3')

    def remove(self):
        self.manage('disable')
        self.unit.unlink()
        execute('systemctl', '--user', 'daemon-reload')


def main(installed):
    manager = ScheduledTask if os.name == 'nt' else UserUnit
    miner = installed / 'bin' / ('canary-miner.exe' if os.name == 'nt' else 'canary-miner')
    march, april, may = map(utc, ('2023-03-01T00:00:00', '2023-03-31T23:00:00', '2023-04-30T23:00:00'))
    with tempfile.TemporaryDirectory(prefix='canary-watch-') as directory:
        label = 'test-' + Path(directory).name.rsplit('-', 1)[-1].replace('_', 'x')
        name = 'Billing & \u7535' if os.name == 'nt' else 'Billing $HOME %n \\" = \u7535 '
        workspace = Path(directory) / name
        workspace.mkdir()
        seed = workspace / 'march \u7535.cmr'

        def bill(*args, expected=0):
            return execute(miner, 'meter-bill', *args, cwd=workspace, expected=expected)

        prior = cycle(march, april, GO_PRODUCT, Decimal(20), 12, 34, 31)
        following = cycle(april, may, GO_PRODUCT, Decimal('42.290'), 15, 40, 30)
        originals = [workspace / name for name in ('initial-tariff.json', 'initial-readings.json', 'initial-rates.json')]
        for file, content in zip(originals, prior):
            stage(file, content)
        bill('--tariff', originals[0], '--intervals', originals[1], '--octopus-rates', originals[2],
             '--save-review', 'seed.cmr', '--as-of-utc-ms', str(april + HOUR))
        (workspace / 'seed.cmr').rename(seed)
        saved = seed.read_bytes()
        deployed = manager(installed, seed, label)

        def workspace_busy():
            result = subprocess.run((miner, 'meter-bill', '--roll-forward', seed), cwd=workspace,
                                    capture_output=True, timeout=30)
            assert result.returncode in (0, 3), result
            return result.returncode == 3 and b'workspace is busy' in result.stderr

        target = workspace / f'cycle-{may}.cmr'
        try:
            deployed.start()
            wait_until(workspace_busy)
            stage(workspace / f'cycle-{may}.readings.json', following[1])
            stage(workspace / f'cycle-{may}.rates.json', following[2])
            wait_until(target.exists)
            report = bill('--open-review', target, '--previous-cycle', seed)
            assert f'Cycle estimate at closing {following[3]:.6f} GBP'.encode('ascii') in report, report
            archived = target.read_bytes()
            deployed.stop()
            assert b'1 verified, 0 saved' in bill('--roll-forward', seed)
            deployed.start()
            wait_until(workspace_busy)
            seed.write_bytes(b'changed seed')
            wait_until(deployed.refused)
            seed.write_bytes(saved)
            time.sleep(2)
            assert deployed.refused()  # Restoring the input does not restart the task.
            assert seed.read_bytes() == saved and target.read_bytes() == archived
            assert b'1 verified, 0 saved' in bill('--roll-forward', seed)
        finally:
            try:
                deployed.stop()
            finally:
                deployed.remove()
    print(f'Billing watch deployment passed with {manager.__name__}: start, archive, stop, refusal, removal.')


if __name__ == '__main__':
    main(Path(sys.argv[1]).resolve(strict=True))
