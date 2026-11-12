"""Actual inert main/genuine helper; synthetic argv and owned files only. Never a real device or pool."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    cli, helper = [Path(value).resolve() for value in sys.argv[1:3]]
    command_error = b'unknown command (run, list-devices, selftest, status, version, settings, diagnostics; or start with an option, e.g. --pool); rejected original omitted\n'
    tuning_error = b'unknown GPU tuning flag; rejected original omitted\n'
    pool_error = b'pool is neither a preset (kryptex, herominers, f2pool), HOST:PORT, nor stratum+tcp://HOST:PORT; rejected original omitted\n'
    option_error = b'unknown option. Run with --help for the list; rejected original omitted.\n'
    marker = 'fixture.private-cli-original'
    payloads = [marker, marker+"'", marker+'\x1b[2J', marker+'\r\nhidden', marker+'\u202e', marker+'x'*4096]
    failures = []
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-cli-errors-') as directory:
        root = Path(directory)
        binary = root/('cli-errors'+('.exe' if os.name == 'nt' else ''))
        shutil.copy2(cli, binary)
        shutil.copy2(helper, root/('canary_preferences_worker'+('.exe' if os.name == 'nt' else '')))
        unrelated = root/'original.txt'
        unrelated.write_bytes(b'fixture-original-preserved')
        original_names = {path.name for path in root.iterdir()}

        def invoke(args):
            return subprocess.run([str(binary), *args], cwd=root, capture_output=True, timeout=15)

        def rejected(name, args, code, expected):
            nonlocal checks
            result = invoke(args)
            output = (result.stdout+result.stderr).replace(b'\r\n', b'\n')
            assert result.returncode == code and b'FORBIDDEN_DEVICE_SETUP' not in output, name
            assert unrelated.read_bytes() == b'fixture-original-preserved' and not (root/'state').exists(), name
            assert {path.name for path in root.iterdir()} == original_names, name
            if output != expected or marker.encode() in output or b'\x1b' in output:
                failures.append(name)
            checks += 1

        for number, payload in enumerate(payloads):
            for route, args, code, expected in (
                ('command', [payload], 2, command_error),
                ('tuning', ['--oc-'+payload], 2, tuning_error),
                ('pool', ['--pool', 'stratum+tcp://fixture-user:'+payload+'@fixture.invalid:3333'], 3, pool_error),
                ('legacy-option', ['--fixture-'+payload], 1, option_error)):
                rejected(route+'-'+str(number), args, code, expected)
        for name, args, code, expected in (
            ('api-port', ['--api-port', 'x'], 2, b'--api-port needs a port number (0 disables the endpoint)\n'),
            ('language', ['--language', marker], 2, b'--language requires one supported exact locale ID; rejected value not applied.\n'),
            ('wallet-shape', ['--wallet', '.fixture'], 3, b'--wallet must be ADDRESS or ADDRESS.WORKER (one dot)\n'),
            ('api-conflict', ['--api-enable', '--api-port', '0'], 3, b'--api-enable conflicts with --api-port 0\n'),
            ('strict-tls-ip', ['--pool', 'stratum+ssl://127.0.0.1:443'], 3, b'strict pool TLS requires an explicit ASCII DNS host and port; private original omitted; no TCP fallback\n')):
            rejected(name, args, code, expected)
        for args in (['version'], ['--help']):
            result = invoke(args)
            assert result.returncode == 0 and b'FORBIDDEN_DEVICE_SETUP' not in result.stdout+result.stderr, 'unchanged-positive-help-version'
            assert b'CanaryMiner' in result.stdout
            checks += 1
        result = invoke(['list-devices'])
        assert result.returncode == 97 and b'FORBIDDEN_DEVICE_SETUP' in result.stderr, 'valid entry must still reach the inert admission sentinel'
        assert unrelated.read_bytes() == b'fixture-original-preserved' and not (root/'state').exists()
        assert {path.name for path in root.iterdir()} == original_names
        checks += 1
    assert not failures, 'Closed CLI rejection envelope failed in: '+', '.join(failures) # IDs only; never echo rejected output
    print('cli_error_privacy:', checks, 'closed rejection/control/preservation/positive cases; no real device or network')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    main()
