"""One-shot public ticker CLI tested with an inert installed sibling and isolated files."""
from hashlib import sha256
from io import BytesIO
import os
import re
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time


def skewed_receipts():
    utc = 1760000000
    report = ("Historical public ticker sample (ages at capture)\n"
              f"Capture UTC {utc+33} | no trade-event timestamps or current freshness claim\n"
              f"Gate Pearl mainnet PRL/USDT: 2.5 | observed UTC {utc} | age at capture 33 s\n"
              f"  Request start UTC {utc} | receipt UTC {utc+1}\n"
              f"Kraken USDT/USD: 0.98 | observed UTC {utc+2} | age at capture 31 s\n"
              f"  Request start UTC {utc+2} | receipt UTC {utc+33}\n"
              "PRL/USD reference at capture: N/A (observations or receipts more than 30 s apart)\n")

    def receipt(sent, received, body):
        wire = f"CMJSON1 0 200 {utc+sent} 0 {len(body)}\n".encode() + body
        return f"{utc+sent}\n{utc+received}\n{received*1000+1}\n{len(wire)}\n".encode() + wire

    data = (b"CANARY-MARKET-TICKER-SAMPLE-1\n" + f"{utc}\n{utc+33}\n33001\n".encode() +
            receipt(0, 1, b'[{"currency_pair":"PEARL_USDT","last":"2.50"}]') +
            receipt(2, 33, b'{"error":[],"result":{"USDTZUSD":{"c":["0.98","1"]}}}') +
            f"{len(report)}\n".encode() + report.encode())
    return data + sha256(data).hexdigest().encode() + b"\n", report


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
        archive = work / 'sample.cmt'
        temporary = work / 'sample.cmt.tmp'
        save = (allow, '--save-review', archive.name)
        sample = (allow, '--save-sample', archive.name)
        resolver = '192.168.50.53'
        dns = ('--dns-server', resolver)
        doh = ('--dns-server', 'cloudflare-doh')
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
        archived, skew_report = skewed_receipts()
        archive.write_bytes(archived)
        reopened = run('--open-review', archive)
        assert reopened.returncode == 0 and reopened.stdout == skew_report and not reopened.stderr, reopened
        assert archive.read_bytes() == archived and not requests.exists()
        archive.unlink()
        shutil.copy2(fixture, helper)
        for args in ((), (allow, allow), (allow, '--no-market'), (allow, '--config', config.name),
                     (allow+'=true',), ('--help', allow), ('--allow-public-ticker\uff53',),
                     ('--url', 'private-sentinel'), dns, doh, (allow, '--dns-server'),
                     (allow, '--dns-server', '8.8.8.8:53'), (allow, *dns, *dns),
                     (allow, '--dn\uff53-server', resolver), (allow, '--dns-server', '\uff11' + resolver[1:]),
                     ('--file', config.name, *dns), save[1:], (*save, *save[1:]),
                     sample[1:], (*save, *sample[1:]), (allow, '--sav\uff45-sample', archive.name),
                     (allow, '--save-review', 'other/path.cmt'), (allow, '--save-review', 'CON.cmt'),
                     (allow, '--save-review', 'sample.cmh'), (allow, '--sav\uff45-review', archive.name),
                     (allow, '--save-review', '\uff53ample.cmt'), ('--open-review',),
                     ('--open-review', archive.name, allow)):
            result = run(*args)
            assert result.returncode == 2 and not result.stdout and not requests.exists(), result
        result = invoke('market-ticker\uff53', allow)
        assert result.returncode == 2 and not result.stdout and not requests.exists(), result
        assert not resolver_requests.exists()
        if not capable:
            for args in ((allow,), (allow, *dns), (allow, *doh), save, (*save, *dns), sample, (*sample, *dns)):
                result = run(*args)
                assert result.returncode == 3 and not result.stdout and 'HTTPS unavailable in this build' in result.stderr
            assert not requests.exists() and not resolver_requests.exists() and not archive.exists() and not temporary.exists()
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
        identity_requests = install / 'identity-requests'
        identity_scenario = install / 'identity-scenario'
        assert identity_requests.read_text(encoding='ascii').splitlines() == ['12']
        identity_requests.unlink()

        for case in ('perle', 'wrapped', 'old-helper', 'limited'):
            identity_scenario.write_text(case, encoding='ascii')
            denied = run(allow)
            assert denied.returncode == 3 and not denied.stderr, denied
            assert 'Latest PRL/USD reference: N/A' in denied.stdout
            assert 'Latest USDT/USD [Kraken USDTZUSD]: 0.98' in denied.stdout
            if case in ('perle', 'wrapped'):
                assert 'public market asset identity could not be verified' in denied.stdout
            assert_requests(['2'])
            assert identity_requests.read_text(encoding='ascii').splitlines() == ['12']
            identity_requests.unlink()
        identity_scenario.write_text('perle', encoding='ascii')
        denied = run(*sample)
        assert denied.returncode == 3 and 'public market asset identity could not be verified' in denied.stdout
        assert run('--open-review', archive).stdout == denied.stdout
        assert_requests(['2'])
        archive.unlink()
        assert identity_requests.read_text(encoding='ascii').splitlines() == ['12']
        identity_requests.unlink()
        identity_scenario.unlink()

        identity_scenario.write_text('budget', encoding='ascii')
        shared_budget = run(allow)
        assert shared_budget.returncode == 0 and 'Latest PRL/USD reference: 2.45' in shared_budget.stdout
        assert 0 < int((install / 'ticker-budget').read_text()) <= int((install / 'identity-budget').read_text()) - 1150
        assert_requests(['0', '2'])
        assert identity_requests.read_text(encoding='ascii').splitlines() == ['12']
        identity_requests.unlink()
        identity_scenario.unlink()

        assert not resolver_requests.exists()
        resolved = run(allow, *dns)
        assert resolved.returncode == 0 and not resolved.stderr and resolver not in resolved.stdout, resolved
        assert 'Latest PRL/USD reference: 2.45' in resolved.stdout
        assert_requests(['0', '2'])
        assert resolver_requests.read_text(encoding='ascii').splitlines() == [resolver, resolver]
        assert identity_requests.read_text(encoding='ascii').splitlines() == [f'12 {resolver}']
        identity_requests.unlink()
        resolver_requests.unlink()

        scenario.write_text('archive', encoding='ascii')
        for args, selected in (((*save, *dns), resolver), ((allow, *dns, *save[1:]), resolver),
                               ((*save, *doh), doh[1])):
            captured = run(*args)
            assert captured.returncode == 0 and not captured.stderr, captured
            assert 'Historical public ticker capture' in captured.stdout and 'PRL/USD reference at capture: 2.45' in captured.stdout
            stored = archive.read_bytes()
            fields = BytesIO(stored)
            assert fields.readline() == b'CANARY-MARKET-TICKER-REVIEW-1\n'
            assert all(int(fields.readline()) > 0 for _ in range(3))
            for feed in ('0', '2'):
                assert all(int(fields.readline()) > 0 for _ in range(4))
                length = int(fields.readline())
                original = (install / ('ticker-body-' + feed)).read_bytes()
                assert original.startswith(b'\t') and original.endswith(b'\r\n  ')
                assert length == len(original) and fields.read(length) == original
            assert fields.read(int(fields.readline())) == captured.stdout.encode('ascii')
            assert selected not in captured.stdout and selected.encode('ascii') not in stored
            assert_requests(['0', '2'])
            assert resolver_requests.read_text(encoding='ascii').splitlines() == [selected, selected]
            resolver_requests.unlink()
            replay = run('--open-review', str(archive))
            assert replay.returncode == 0 and replay.stdout == captured.stdout and not replay.stderr, replay
            assert archive.read_bytes() == stored and not requests.exists() and not resolver_requests.exists()
            assert not temporary.exists()
            repeated = run(*save)
            assert repeated.returncode == 3 and not repeated.stdout and 'existing archive was preserved' in repeated.stderr
            assert_requests(['0', '2'])
            assert archive.read_bytes() == stored and not temporary.exists()
            archive.unlink()
        scenario.unlink()
        scenario.write_text('successful-retry-header', encoding='ascii')
        legacy = run(*save)
        assert legacy.returncode == 0 and 'Retry-After' not in legacy.stdout
        assert run('--open-review', archive).stdout == legacy.stdout
        assert_requests(['0', '2'])
        archive.unlink()
        complete_sample = run(*sample)
        assert complete_sample.returncode == 0 and 'PRL/USD reference at capture: 2.45' in complete_sample.stdout
        assert complete_sample.stdout.count('Retry-After 2 s') == 2
        assert run('--open-review', archive).stdout == complete_sample.stdout
        assert_requests(['0', '2'])
        archive.unlink()
        scenario.unlink()
        if os.name == 'posix':
            reader, writer = os.pipe()
            os.close(reader)
            try:
                with subprocess.Popen([str(cli), 'market-tickers', *save], cwd=work,
                                      stdout=writer, stderr=subprocess.PIPE) as child:
                    _, error = child.communicate(timeout=8)
                    assert child.returncode == 1 and not error, error
            finally:
                os.close(writer)
            assert_requests(['0', '2'])
            assert archive.exists() and not temporary.exists()
            preserved = run('--open-review', str(archive))
            assert preserved.returncode == 0 and 'PRL/USD reference at capture: 2.45' in preserved.stdout
            archive.unlink()

        temporary.write_bytes(b'existing-temp')
        refused = run(*save)
        assert refused.returncode == 3 and not refused.stdout and not archive.exists()
        assert_requests(['0', '2'])
        assert temporary.read_bytes() == b'existing-temp'
        temporary.unlink()

        scenario.write_text('lagged', encoding='ascii')
        late = run(allow)
        assert late.returncode == 0, late
        ages = [int(value) for value in re.findall(r'observation age ([0-9]+)s', late.stdout)]
        assert len(ages) == 2 and ages[0] >= 2, late
        assert_requests(['0', '2'])
        assert not resolver_requests.exists()

        for mode, valid, failed, saved_detail in (
            ('foreign', 'Latest PRL/USDT [Gate international PEARL_USDT; Pearl mainnet]: 2.5', '87213',
             'Gate Pearl mainnet PRL/USDT: 2.5'),
            ('future', 'Latest USDT/USD [Kraken USDTZUSD]: 0.98', 'Latest PRL/USD reference: 2.45',
             'Kraken USDT/USD: 0.98'),
            ('limited', 'public provider HTTP error', 'Latest PRL/USD reference: 2.45', 'HTTP 429 | Retry-After 1 s'),
        ):
            scenario.write_text(mode, encoding='ascii')
            result = run(allow)
            assert result.returncode == 3 and not result.stderr, result
            assert valid in result.stdout and failed not in result.stdout, result
            assert 'Latest PRL/USD reference: N/A' in result.stdout, result
            assert_requests(['0', '2'])
            rejected_capture = run(*save)
            assert rejected_capture.returncode == 3 and not rejected_capture.stdout and not archive.exists()
            assert 'both valid, fresh, aligned observations required' in rejected_capture.stderr
            assert_requests(['0', '2'])
            captured = run(*sample, *dns)
            assert captured.returncode == 3 and not captured.stderr, captured
            assert 'Historical public ticker sample' in captured.stdout and 'PRL/USD reference at capture: N/A' in captured.stdout
            assert saved_detail in captured.stdout, captured
            saved = archive.read_bytes()
            assert saved.startswith(b'CANARY-MARKET-TICKER-SAMPLE-1\n')
            assert resolver.encode() not in saved and not temporary.exists()
            assert_requests(['0', '2'])
            assert resolver_requests.read_text(encoding='ascii').splitlines() == [resolver, resolver]
            resolver_requests.unlink()
            replay = run('--open-review', archive)
            assert replay.returncode == 0 and replay.stdout == captured.stdout and not replay.stderr, replay
            assert archive.read_bytes() == saved and not requests.exists()
            archive.unlink()

        if os.name != 'nt':
            for mode, launch_log, feed in ((identity_scenario, identity_requests, '12'), (scenario, requests, '0')):
                identity_requests.unlink(missing_ok=True)
                mode.write_text('slow', encoding='ascii')
                with subprocess.Popen([str(cli), 'market-tickers', *sample], cwd=work, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE, text=True) as child:
                    deadline = time.monotonic() + 4
                    try:
                        while time.monotonic() < deadline and child.poll() is None:
                            if launch_log.exists() and launch_log.read_text().splitlines() == [feed]:
                                break
                            time.sleep(0.02)
                        else:
                            raise AssertionError(f'no helper launch for feed {feed} before interruption')
                        child.send_signal(signal.SIGINT)
                        output, error = child.communicate(timeout=4)
                        assert child.returncode == 130 and not output and error == 'market-tickers: sample cancelled\n'
                    finally:
                        if child.poll() is None:
                            child.kill()
                            child.wait(timeout=3)
                mode.unlink()
                assert identity_requests.read_text(encoding='ascii').splitlines() == ['12']
                if feed == '0':
                    assert_requests(['0'])
                else:
                    assert not requests.exists()
                assert not archive.exists() and not temporary.exists()

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
