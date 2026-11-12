"""Ordinary TLS admission through actual inert main and genuine preference helper. No device or network runs."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    cli, helper = [Path(value).resolve() for value in sys.argv[1:3]]
    available = sys.argv[3].lower() in ('on', 'true', '1')
    assert sys.argv[3].lower() in ('on', 'true', '1', 'off', 'false', '0')
    checks = 0
    empty = dict(mode='unchanged', tls_url='', plaintext_url='', plaintext_consent=False)
    strict = dict(mode='strict_tls', tls_url='stratum+ssl://pool-fixture.invalid:443', plaintext_url='', plaintext_consent=False)
    clean_env = {key: value for key, value in os.environ.items() if not key.startswith('CANARY_PEARL_')}
    with tempfile.TemporaryDirectory(prefix='canary-pool-tls-cli-') as directory:
        root = Path(directory)
        binary = root / ('miner-cli.exe' if os.name == 'nt' else 'miner-cli')
        shutil.copy2(cli, binary)
        shutil.copy2(helper, root / ('canary_preferences_worker.exe' if os.name == 'nt' else 'canary_preferences_worker'))
        config_path = root / 'config.json'
        prefs_path = root / 'pool-security-preferences.json'
        base = dict(schema_version=1, state_dir=str(root), donation=False,
                    wallet='prl1' + 'q' * 59, pools=['stratum+ssl://pool-fixture.invalid:443'])

        def case(name, config=None, prefs=None, args=(), admitted=False, code=3, env=None, with_config=True):
            nonlocal checks
            config_path.write_text(json.dumps(base if config is None else config), encoding='utf-8')
            if prefs is None:
                if prefs_path.exists():
                    prefs_path.unlink()
            else:
                prefs_path.write_text(json.dumps(prefs), encoding='utf-8')
            originals = {path.name: path.read_bytes() for path in root.iterdir() if path.is_file()}
            # This shared inert CLI also tests non-Pearl legacy admission; production builds select Pearl by default.
            command = [str(binary), '--algo', 'pearl-pow', *args]
            if with_config:
                command += ['--config', str(config_path)]
            result = subprocess.run(command, cwd=root, env={**clean_env, **(env or {})}, capture_output=True, timeout=15)
            output = result.stdout + result.stderr
            expected = 97 if admitted and available else code
            assert result.returncode == expected, (name, result.returncode, output.decode('utf-8', 'replace'))
            assert (b'FORBIDDEN_DEVICE_SETUP' in output) == (expected == 97), name
            assert b'pool-fixture.invalid' not in output and b'other-fixture.invalid' not in output, name
            assert {path.name: path.read_bytes() for path in root.iterdir() if path.is_file()} == originals, name
            assert not any(path.is_dir() for path in root.iterdir()), name
            checks += 1
            return result

        case('explicit strict ordinary', admitted=True)
        case('explicit strict CLI', config={**base, 'pools': ['pool-fixture.invalid:1']}, args=['--pool', strict['tls_url'], '--pearl-network', 'mainnet', '--pearl-target-semantics', 'base-before-penalty'], admitted=True)
        case('explicit strict no donation CLI precedence', config={**base, 'donation': True}, args=['--no-donation'], admitted=True)
        case('strict refuses donation', config={**base, 'donation': True})
        case('f2pool account only', config={**base, 'wallet': '', 'pools': [dict(url='stratum+ssl://pearl.f2pool.com:5501', account='synthetic_account')]}, admitted=True)
        prefs = dict(schema_version=1, user_pools=[strict], donation=empty)
        tcp = {**base, 'pools': ['pool-fixture.invalid:1']}
        case('saved strict same host port mapping', config=tcp, prefs=prefs, admitted=True)
        case('saved strict same exact SSL', prefs=prefs, admitted=True)
        case('saved strict different host', config={**tcp, 'pools': ['other-fixture.invalid:1']}, prefs=prefs)
        case('saved strict different explicit port', config={**base, 'pools': ['stratum+ssl://pool-fixture.invalid:444']}, prefs=prefs)
        case('saved strict missing source pool', config={key: value for key, value in base.items() if key != 'pools'}, prefs=prefs)
        case('prefer with consent still refuses', config=tcp, prefs={**prefs, 'user_pools': [{**strict, 'mode': 'prefer_tls', 'plaintext_url': 'stratum+tcp://pool-fixture.invalid:1', 'plaintext_consent': True}]})
        case('donation policy never inherits user permission', prefs={**prefs, 'donation': strict})
        case('unused secure preference slot', prefs={**prefs, 'user_pools': [empty, strict]})
        case('unused explicit secure pool', config={**base, 'pools': ['pool-fixture.invalid:1', strict['tls_url']]})
        for number, url in enumerate(['stratum+ssl://127.0.0.1:443', 'stratum+ssl://[::1]:443', 'stratum+ssl://user@pool-fixture.invalid:443',
                                     'stratum+ssl://pool-fixture.invalid:443/path', 'stratum+ssl://stratum+tcp://pool-fixture.invalid:443']):
            case('invalid secure identity ' + str(number), config={**base, 'pools': [url]})
        for number, args in enumerate([['--bench'], ['--autotune'], ['--pearl-transcript', 'private-transcript'], ['--gpu-cclock', '1'],
                                     ['--pearl-authorization-mode', 'solo'], ['--pearl-work-range', '0x0000000000000000:0x0000000000000001']]):
            result = case('research CLI ' + str(number), args=args)
            assert not (root / 'private-transcript').exists()
        # These flags are parsed after the first product admission. They still cannot reach the device sentinel.
        for name, value in [('CANARY_PEARL_NATIVE_EVIDENCE', '1'), ('CANARY_PEARL_PROFILE_INTERVAL', '1'), ('CANARY_PEARL_PRODUCTION_MODE', '1')]:
            result = subprocess.run([str(binary), '--algo', 'pearl-pow', '--config', str(config_path)], cwd=root, env={**clean_env, name: value}, capture_output=True, timeout=15)
            assert result.returncode not in (0, 97) and b'FORBIDDEN_DEVICE_SETUP' not in result.stdout + result.stderr, name
            checks += 1

        frozen = ['--worker-child', '--worker-id', '0', '--worker-pool-tls', strict['tls_url'], '--no-donation']
        case('worker frozen original TLS', args=frozen, admitted=True)
        case('worker reordered private policy', args=['--worker-pool-tls', strict['tls_url'], '--no-donation', '--worker-id', '0', '--worker-child'], admitted=True)
        case('worker source changed to TCP cannot downgrade', config=tcp, args=frozen, admitted=True)
        case('worker donation changed remains disabled', config={**tcp, 'donation': True}, args=frozen, admitted=True)
        case('worker host changed refuses', config={**tcp, 'pools': ['other-fixture.invalid:1']}, args=frozen)
        case('worker missing pool refuses', config={key: value for key, value in base.items() if key != 'pools'}, args=frozen)
        case('worker no frozen peer refuses', args=['--worker-child', '--worker-id', '0'])
        case('parent cannot use private policy', args=['--worker-pool-tls', strict['tls_url']])
        case('parent reordered private policy', args=['--worker-pool-tls', strict['tls_url'], 'ignored'], code=1)
        case('duplicate private policy', args=frozen + ['--worker-pool-tls', strict['tls_url']], code=2)
        case('missing private policy value', args=['--worker-child', '--worker-pool-tls'], code=2)
        case('wrong private protocol refuses', args=['--worker-child', '--worker-id', '0', '--worker-pool-tls', 'stratum+tcp://pool-fixture.invalid:443'])
        # Save changes in the parent settings file are ignored by the child's already-frozen policy.
        case('worker does not reload new saved fallback', config=tcp, prefs={**prefs, 'user_pools': [{**strict, 'mode': 'prefer_tls'}]}, args=frozen, admitted=True)
        for args in (['--help', '--pool', strict['tls_url']], ['--pool', strict['tls_url'], '--help']):
            case('help no TLS admission', args=args, code=0, with_config=False)
    print(f'pool TLS runtime CLI: {checks} actual entry/policy/preservation checks; runtime={available}; no real device or network')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    main()
