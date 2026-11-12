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

        def case(name, config=None, prefs=None, args=(), admitted=False, code=3, env=None, with_config=True, fallback_warning=False):
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
            warned = b'Plaintext exposes login/proof data, and a network attacker can trigger fallback.' in output
            assert warned == (fallback_warning and expected == 97), name
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
        prefer = {**strict, 'mode': 'prefer_tls', 'plaintext_url': 'stratum+tcp://pool-fixture.invalid:2', 'plaintext_consent': True}
        prefer_prefs = {**prefs, 'user_pools': [prefer]}
        case('prefer without consent stays TLS only', config=tcp, prefs={**prefs, 'user_pools': [{**strict, 'mode': 'prefer_tls'}]}, admitted=True)
        case('prefer TCP declaration without consent stays TLS only', config=tcp, prefs={**prefs, 'user_pools': [{**prefer, 'plaintext_consent': False}]}, admitted=True)
        case('prefer with consent admits independent TCP port', config=tcp, prefs=prefer_prefs, admitted=True, fallback_warning=True)
        case('prefer explicit SSL remains strict', prefs=prefer_prefs, admitted=True)
        case('prefer explicit CLI SSL remains strict', config=tcp, prefs=prefer_prefs,
             args=['--pool', strict['tls_url'], '--pearl-network', 'mainnet', '--pearl-target-semantics', 'base-before-penalty'], admitted=True)
        case('prefer saved TLS host conflicts', config={**tcp, 'pools': ['other-fixture.invalid:1']}, prefs=prefer_prefs)
        case('prefer saved TLS port conflicts with explicit SSL', config={**base, 'pools': ['stratum+ssl://pool-fixture.invalid:444']}, prefs=prefer_prefs)
        case('prefer cannot authorize different TCP host', config=tcp, prefs={**prefs, 'user_pools': [{**prefer, 'plaintext_url': 'stratum+tcp://other-fixture.invalid:2'}]})
        case('prefer cannot omit consented TCP endpoint', config=tcp, prefs={**prefs, 'user_pools': [{**prefer, 'plaintext_url': ''}]})
        case('prefer requires donation off', config={**tcp, 'donation': True}, prefs=prefer_prefs)
        case('donation policy never inherits user permission', prefs={**prefs, 'donation': strict})
        case('inactive donation TLS candidate stays inactive', prefs={**prefs, 'donation': {**empty, 'tls_url': strict['tls_url']}}, admitted=True)
        case('inactive donation TCP candidate stays inactive', prefs={**prefs, 'donation': {**empty, 'plaintext_url': prefer['plaintext_url']}}, admitted=True)
        case('inactive donation candidates do not block legacy TCP', config=tcp,
             prefs={**prefs, 'user_pools': [empty], 'donation': {**empty, 'tls_url': strict['tls_url'], 'plaintext_url': prefer['plaintext_url']}}, code=97)
        case('unused secure preference slot', prefs={**prefs, 'user_pools': [empty, strict]})
        case('unused explicit secure pool', config={**base, 'pools': ['pool-fixture.invalid:1', strict['tls_url']]})
        for number, url in enumerate(['stratum+ssl://127.0.0.1:443', 'stratum+ssl://[::1]:443', 'stratum+ssl://user@pool-fixture.invalid:443',
                                     'stratum+ssl://pool-fixture.invalid:443/path', 'stratum+ssl://stratum+tcp://pool-fixture.invalid:443']):
            case('invalid secure identity ' + str(number), config={**base, 'pools': [url]})
        for number, args in enumerate([['--bench'], ['--autotune'], ['--pearl-transcript', 'private-transcript'], ['--gpu-cclock', '1'],
                                     ['--pearl-authorization-mode', 'solo'], ['--pearl-work-range', '0x0000000000000000:0x0000000000000001']]):
            result = case('research CLI ' + str(number), args=args)
            # Benchmark never loads pool preferences; the TCP source has no active TLS or fallback policy there.
            case('prefer research CLI ' + str(number), config=tcp, prefs=prefer_prefs, args=args, code=97 if args == ['--bench'] else 3)
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
        frozen_prefer = frozen + ['--worker-pool-fallback', prefer['plaintext_url']]
        case('worker accepts frozen fallback from TCP source', config=tcp, args=frozen_prefer, admitted=True)
        case('worker reordered frozen fallback', config=tcp,
             args=['--worker-pool-fallback', prefer['plaintext_url'], '--worker-pool-tls', strict['tls_url'], '--no-donation', '--worker-id', '0', '--worker-child'], admitted=True)
        case('worker source now explicit SSL refuses frozen fallback', args=frozen_prefer)
        case('worker original explicit SSL CLI refuses frozen fallback', config=tcp, args=frozen_prefer + ['--pool', strict['tls_url']])
        case('worker frozen fallback does not follow changed TCP port', config={**tcp, 'pools': ['pool-fixture.invalid:9']}, args=frozen_prefer, admitted=True)
        case('worker frozen fallback preserves donation off', config={**tcp, 'donation': True}, args=frozen_prefer, admitted=True)
        case('worker fallback requires TLS private flag', config=tcp, args=['--worker-child', '--worker-id', '0', '--worker-pool-fallback', prefer['plaintext_url']])
        case('parent cannot use fallback private flag', config=tcp, args=['--worker-pool-tls', strict['tls_url'], '--worker-pool-fallback', prefer['plaintext_url']])
        case('parent cannot use lone fallback private flag', config=tcp, args=['--worker-pool-fallback', prefer['plaintext_url']])
        case('duplicate fallback private flag', config=tcp, args=frozen_prefer + ['--worker-pool-fallback', prefer['plaintext_url']], code=2)
        case('missing fallback private value', config=tcp, args=frozen + ['--worker-pool-fallback'], code=2)
        for number, url in enumerate(['stratum+tcp://other-fixture.invalid:2', 'stratum+ssl://pool-fixture.invalid:2',
                                     'stratum+tcp://POOL-fixture.invalid:2', 'stratum+tcp://pool-fixture.invalid:02',
                                     'stratum+tcp://pool-fixture.invalid:2/path', 'pool-fixture.invalid:2']):
            case('invalid frozen fallback ' + str(number), config=tcp, args=frozen + ['--worker-pool-fallback', url])
        # Save changes in the parent settings file are ignored by the child's already-frozen policy.
        case('worker does not reload new saved fallback', config=tcp, prefs={**prefs, 'user_pools': [{**strict, 'mode': 'prefer_tls'}]}, args=frozen, admitted=True)
        case('worker ignores new saved consent', config=tcp, prefs=prefer_prefs, args=frozen, admitted=True)
        case('worker fallback survives later preference revocation', config=tcp, prefs=prefs, args=frozen_prefer, admitted=True)
        case('worker fallback ignores later saved peer edit', config=tcp,
             prefs={**prefs, 'user_pools': [{**prefer, 'tls_url': 'stratum+ssl://other-fixture.invalid:444', 'plaintext_url': 'stratum+tcp://other-fixture.invalid:3'}]}, args=frozen_prefer, admitted=True)
        for args in (['--help', '--pool', strict['tls_url']], ['--pool', strict['tls_url'], '--help']):
            case('help no TLS admission', args=args, code=0, with_config=False)
    print(f'pool TLS runtime CLI: {checks} actual entry/policy/preservation checks; runtime={available}; no real device or network')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    main()
