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

from test_meter_bill_named_cli import HOUR, SCOPE, utc
from test_octopus_cycle_review_cli import cycle
from test_octopus_meter_rates_cli import GO_PRODUCT


def execute(*args, cwd=None, env=None, expected=0, reason=b''):
    result = subprocess.run(args, cwd=cwd, env=env, capture_output=True, timeout=30)
    assert result.returncode == expected and reason in result.stderr, result
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
    def __init__(self, installed, seed, label, *, capture_python=None, watch_invoices=False):
        self.powershell = ('powershell.exe', '-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'RemoteSigned')
        command = (*self.powershell, '-File', str(installed / 'deploy' / 'Register-BillingWatch.ps1'),
                   '-Name', label, '-Seed', str(seed), '-Seconds', '60' if capture_python else '1')
        if capture_python:
            command += ('-AccountCapturePython', str(capture_python))
        if watch_invoices:
            command += ('-WatchInvoices',)
        name = execute(*command).decode('ascii').strip()
        kind = 'OctopusCapture' if capture_python else 'Billing'
        assert re.fullmatch(rf'CanaryMiner-{kind}-{label}-S-1-[0-9-]+', name), name
        self.env = dict(os.environ, CANARY_BILLING_TEST_TASK=name)
        try:
            duplicate = subprocess.run(command, capture_output=True, timeout=30)
            assert duplicate.returncode != 0, duplicate
            xml = self.manage('Export-ScheduledTask').decode('ascii', errors='ignore')
            for property_value in ('LogonTrigger', '--run-scheduled' if capture_python else '--watch-quiet'):
                assert property_value in xml, property_value
            assert '--allow-public-rates' not in xml
            assert ('--confirm-electricity-import' in xml) == bool(capture_python)
            assert ('--watch-invoices' in xml) == watch_invoices
            assert self.manage('(Get-ScheduledTask', ').Principal.LogonType').strip() == b'Interactive'
            assert self.manage('(Get-ScheduledTask', ').Principal.RunLevel').strip() == b'Limited'
            assert self.manage('(Get-ScheduledTask', ').Settings.MultipleInstances').strip() == b'IgnoreNew'
            assert self.manage('(Get-ScheduledTask', ').Settings.ExecutionTimeLimit').strip() == b'PT0S'
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
        return self.state() == b'Ready' and self.last_result() == b'3'

    def last_result(self):
        return self.manage('(Get-ScheduledTaskInfo', ').LastTaskResult').strip()

    def status(self):
        return self.state(), self.last_result()

    def remove(self):
        self.manage('Unregister-ScheduledTask', ' -Confirm:$false')


class UserUnit:
    def __init__(self, installed, seed, label, *, capture_python=None, watch_invoices=False):
        self.name = f'canaryminer-billing-{label}.service'
        config = Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config'))
        self.unit = config / 'systemd' / 'user' / self.name
        self.unit.parent.mkdir(parents=True, exist_ok=True)
        renderer = installed / 'deploy' / 'billing-watch.sh'
        if capture_python:
            data = execute(renderer, seed, '60', '--account-capture', capture_python)
            assert b'--run-scheduled' in data and b'--confirm-electricity-import' in data
            execute(renderer, seed, '1', '--account-capture', capture_python, expected=2)
        else:
            options = ('--watch-invoices',) if watch_invoices else ()
            data = execute(renderer, seed, '1', *options)
            assert b'--allow-public-rates' not in data and b'--confirm-electricity-import' not in data
            assert (b'--watch-invoices' in data) == watch_invoices
            for option in ('--allow-public-rates', '--confirm-electricity-import', '--watch-invoices'):
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

    def status(self):
        return self.manage('show', '-p', 'ActiveState', '-p', 'ExecMainStatus', '-p', 'Result')

    def remove(self):
        self.manage('disable')
        self.unit.unlink()
        execute('systemctl', '--user', 'daemon-reload')


def main(installed, *, activate_default_target=False):
    manager = ScheduledTask if os.name == 'nt' else UserUnit
    miner = installed / 'bin' / ('canary-miner.exe' if os.name == 'nt' else 'canary-miner')
    march, april, may = map(utc, ('2023-03-01T00:00:00', '2023-03-31T23:00:00', '2023-04-30T23:00:00'))
    with tempfile.TemporaryDirectory(prefix='canary-watch-') as directory:
        label = 'test-' + Path(directory).name.rsplit('-', 1)[-1].replace('_', 'x')
        name = 'Billing & \u7535' if os.name == 'nt' else 'Billing $HOME %n \\" = \u7535 '
        workspace = Path(directory) / name
        workspace.mkdir()
        seed = workspace / 'march \u7535.cmr'

        def bill(*args, **expectation):
            return execute(miner, 'meter-bill', *args, cwd=workspace, **expectation)

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
        lease = workspace / 'meter-billing.lock'
        target = workspace / f'cycle-{may}.cmr'
        try:
            if activate_default_target and os.name != 'nt':
                # The disposable fixture owns the user manager. Let its
                # session target pull in the enabled watcher.
                execute('systemctl', '--user', 'restart', 'default.target')
            else:
                deployed.start()
            wait_until(lease.exists)
            bill('--roll-forward', seed, expected=3, reason=b'workspace is busy')
            stage(workspace / f'cycle-{may}.readings.json', following[1])
            stage(workspace / f'cycle-{may}.rates.json', following[2])
            wait_until(target.exists)
            report = bill('--open-review', target, '--previous-cycle', seed)
            assert f'Cycle estimate at closing {following[3]:.6f} GBP'.encode('ascii') in report, report
            archived = target.read_bytes()
            deployed.stop()
            assert b'1 verified, 0 saved' in bill('--roll-forward', seed)
            lease.unlink()
            deployed.start()
            wait_until(lease.exists)
            bill('--roll-forward', seed, expected=3, reason=b'workspace is busy')
            seed.write_bytes(b'changed seed')
            wait_until(deployed.refused)
            seed.write_bytes(saved)
            time.sleep(2)
            assert deployed.refused()  # Restoring the input does not restart the task.
            assert seed.read_bytes() == saved and target.read_bytes() == archived
            assert b'1 verified, 0 saved' in bill('--roll-forward', seed)
        except AssertionError as error:
            raise AssertionError(deployed.status()) from error
        finally:
            try:
                deployed.stop()
            finally:
                deployed.remove()

        comparison = target.with_suffix('.compared.cmr')
        deployed = manager(installed, seed, label + '-invoices', watch_invoices=True)
        try:
            deployed.start()
            wait_until(lambda: bill('--cycle-history', seed, '--with-invoices').count(b'no invoice review') == 2)
            assert not comparison.exists()
            stage(target.with_suffix('.invoice.json'), dict(schema_version=1, scope=SCOPE, currency='GBP',
                  decimal_places=2, rounding='half_even', cycles=[dict(begin_utc_ms=april, end_utc_ms=may,
                                                                       supply_charge=f'{following[3]:.2f}')]))
            wait_until(comparison.exists)
            assert b'Supply invoice matches: 1 of 1 compared cycles' in bill('--cycle-history', seed, '--with-invoices')
            assert seed.read_bytes() == saved and target.read_bytes() == archived

            def hold_comparison():
                try:
                    comparison.rename(comparison.with_suffix('.cmr.held'))
                except PermissionError as error:
                    if getattr(error, 'winerror', None) == 32:
                        return False
                    raise
                return True

            wait_until(hold_comparison)
            wait_until(deployed.refused)
            assert not comparison.exists()
        except AssertionError as error:
            raise AssertionError(deployed.status()) from error
        finally:
            try:
                deployed.stop()
            finally:
                deployed.remove()
    print(f'Billing watch deployment passed with {manager.__name__}: default and opted-in invoice watch.')


if __name__ == '__main__':
    main(Path(sys.argv[1]).resolve(strict=True))
