"""One-shot public ticker CLI tested with an inert installed sibling and isolated files."""
import os
import re
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time


def main(cli_path, fixture, capable):
    worker_name = 'canary_public_json_worker' + ('.exe' if os.name == 'nt' else '')
    allow = '--allow-public-tickers'
    with tempfile.TemporaryDirectory(prefix='canary-ticker-sample-') as tmp:
        root = Path(tmp)
        install = root / 'installed'
        install.mkdir()
        work = root / 'operator'
        work.mkdir()
        state = work / 'state'
        state.mkdir()
        config = work / 'config.json'
        config.write_text('private-sentinel', encoding='ascii')
        prefs = state / 'runtime-preferences.json'
        prefs.write_text('private-sentinel', encoding='ascii')
        cli = Path(shutil.copy2(cli_path, install / cli_path.name))
        helper = install / worker_name
        shutil.copy2(fixture, work / worker_name)  # Never resolve a helper through cwd.
        requests = install / 'ticker-requests'
        resolver_requests = install / 'resolver-requests'
        resolver = '192.168.50.53'
        dns = ('--dns-server', resolver)
        scenario = install / 'ticker-scenario'

        def invoke(*args):
            return subprocess.run([str(cli), *args], cwd=work, capture_output=True, text=True, timeout=8)

        def run(*extra):
            result = invoke('market-tickers', *extra)
            assert 'private-sentinel' not in result.stdout + result.stderr
            assert 'FORBIDDEN_DEVICE_SETUP' not in result.stdout + result.stderr
            return result

        def assert_requests(expected):
            assert requests.read_text().splitlines() == expected
            requests.unlink()
            assert not (work / requests.name).exists()

        for flag in ('-h', '--help'):
            help_result = run(flag)
            assert help_result.returncode == 0 and not help_result.stderr
            assert 'market-tickers --allow-public-tickers' in help_result.stdout
        assert 'market-tickers' in invoke('--help').stdout
        shutil.copy2(fixture, helper)
        for args in ((), (allow, allow), (allow, '--no-market'), (allow, '--config', config.name),
                     (allow+'=true',), ('--help', allow), ('--allow-public-ticker\uff53',),
                     ('--url', 'private-sentinel'), dns, (allow, '--dns-server'),
                     (allow, '--dns-server', '8.8.8.8:53'), (allow, *dns, *dns),
                     (allow, '--dn\uff53-server', resolver), (allow, '--dns-server', '\uff11' + resolver[1:]),
                     ('--file', config.name, *dns)):
            result = run(*args)
            assert result.returncode == 2 and not result.stdout and not requests.exists(), result
        result = invoke('market-ticker\uff53', allow)
        assert result.returncode == 2 and not result.stdout and not requests.exists(), result
        assert not resolver_requests.exists()
        if not capable:
            for args in ((allow,), (allow, *dns)):
                result = run(*args)
                assert result.returncode == 3 and not result.stdout and 'HTTPS unavailable in this build' in result.stderr
            assert not requests.exists() and not resolver_requests.exists()
            print('ticker CLI: disabled build refused all network admissions')
            return 0

        result = run(allow)
        unavailable = 'verified HTTPS/c-ares backend unavailable'
        if sys.platform == 'linux':
            try:
                libc, version = os.confstr('CS_GNU_LIBC_VERSION').split()
                has_pidfd_spawn = libc == 'glibc' and tuple(map(int, version.split('.')[:2])) >= (2, 40)
            except (AttributeError, ValueError, OSError):
                has_pidfd_spawn = False
            if not has_pidfd_spawn:
                assert result.returncode == 3 and unavailable in result.stdout and not requests.exists(), result
                return 77
        assert result.returncode == 0 and not result.stderr, result
        for field in ('Latest PRL/USDT [Gate international PEARL_USDT; Pearl mainnet]: 2.5',
                      'Latest USDT/USD [Kraken USDTZUSD]: 0.98',
                      'Latest PRL/USD reference: 2.45', 'observed UTC ', 'observation age ',
                      'trade-event time NOT SUPPLIED'):
            assert field in result.stdout, result
        assert '1h' not in result.stdout
        assert_requests(['0', '2'])

        assert not resolver_requests.exists()
        resolved = run(allow, *dns)
        assert resolved.returncode == 0 and not resolved.stderr and resolver not in resolved.stdout, resolved
        assert 'Latest PRL/USD reference: 2.45' in resolved.stdout
        assert_requests(['0', '2'])
        assert resolver_requests.read_text(encoding='ascii').splitlines() == [resolver, resolver]
        resolver_requests.unlink()

        scenario.write_text('lagged', encoding='ascii')
        late = run(allow)
        assert late.returncode == 0, late
        ages = [int(value) for value in re.findall(r'observation age ([0-9]+)s', late.stdout)]
        assert len(ages) == 2 and ages[0] >= 2, late
        assert_requests(['0', '2'])
        assert not resolver_requests.exists()

        for mode, valid, failed in (
            ('foreign', 'Latest PRL/USDT [Gate international PEARL_USDT; Pearl mainnet]: 2.5', '87213'),
            ('future', 'Latest USDT/USD [Kraken USDTZUSD]: 0.98', 'Latest PRL/USD reference: 2.45'),
            ('limited', 'public provider HTTP error', 'Latest PRL/USD reference: 2.45'),
        ):
            scenario.write_text(mode, encoding='ascii')
            result = run(allow)
            assert result.returncode == 3 and not result.stderr, result
            assert valid in result.stdout and failed not in result.stdout, result
            assert 'Latest PRL/USD reference: N/A' in result.stdout, result
            assert_requests(['0', '2'])

        if os.name != 'nt':
            scenario.write_text('slow', encoding='ascii')
            with subprocess.Popen([str(cli), 'market-tickers', allow], cwd=work, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True) as child:
                deadline = time.monotonic() + 4
                try:
                    while time.monotonic() < deadline and child.poll() is None:
                        if requests.exists() and requests.read_text().splitlines() == ['0']:
                            break
                        time.sleep(0.02)
                    else:
                        raise AssertionError('no first helper launch observed before interruption')
                    child.send_signal(signal.SIGINT)
                    output, error = child.communicate(timeout=4)
                    assert child.returncode == 130 and not output and error == 'market-tickers: sample cancelled\n'
                finally:
                    if child.poll() is None:
                        child.kill()
                        child.wait(timeout=3)
            assert_requests(['0'])

        helper.unlink()
        result = run(allow)
        assert result.returncode == 3 and not result.stderr, result
        assert unavailable in result.stdout and 'Latest PRL/USD reference: N/A' in result.stdout
        assert not requests.exists() and not (work / requests.name).exists()
        assert prefs.read_text() == config.read_text() == 'private-sentinel'
        assert set(p.name for p in work.iterdir()) == {config.name, state.name, worker_name}
    print('ticker CLI: isolated sibling, exact pair and FX, partial errors, one request per feed, consent and cancellation')
    return 0


if __name__ == '__main__':
    sys.exit(main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), sys.argv[3] == '1'))
