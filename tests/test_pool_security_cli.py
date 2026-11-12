"""Actual host-stub CLI: saved requests refuse before device setup, no endpoint/credential echo or mutation."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    executable = str(Path(sys.argv[1]).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-pool-security-cli-') as directory:
        root = Path(directory)
        config = root / 'config.json'
        config.write_text(json.dumps({'schema_version': 1, 'state_dir': str(root)}), encoding='utf-8')
        path = root / 'pool-security-preferences.json'
        def run(*args):
            return subprocess.run([executable, *args, '--config', str(config)], capture_output=True, timeout=8)
        initial = run()
        assert initial.returncode == 97 and b'FORBIDDEN_DEVICE_SETUP' in initial.stderr and not path.exists()
        checks += 1
        empty = {'mode': 'unchanged', 'tls_url': '', 'plaintext_url': '', 'plaintext_consent': False}
        selected = {'mode': 'strict_tls', 'tls_url': 'stratum+ssl://private-fixture.test:1234', 'plaintext_url': '', 'plaintext_consent': False}
        for donation in (False, True):
            original = json.dumps({'schema_version': 1, 'user_pools': [empty if donation else selected], 'donation': selected if donation else empty})
            path.write_text(original, encoding='utf-8')
            result = run()
            assert result.returncode == 3 and b'FORBIDDEN_DEVICE_SETUP' not in result.stderr
            assert any(message in result.stderr.lower() for message in (
                b'no mining was started', b'no tcp fallback or mining was started'))
            assert b'private-fixture' not in result.stdout + result.stderr and path.read_text(encoding='utf-8') == original
            checks += 2
        for original in ('{"schema_version":99}', '[]', json.dumps({'schema_version': 1, 'user_pools': [{**selected, 'tls_url': 'stratum+ssl://private-user@fixture.test:1'}], 'donation': empty})):
            path.write_text(original, encoding='utf-8')
            result = run()
            assert result.returncode == 3 and b'pool security declarations unavailable' in result.stderr
            assert b'private-user' not in result.stdout + result.stderr and path.read_text(encoding='utf-8') == original
            checks += 2
        help_result = run('settings', '--help')
        assert help_result.returncode == 0 and path.read_text(encoding='utf-8') == original
        checks += 1
        original = json.dumps({'schema_version': 1, 'user_pools': [empty], 'donation': empty})
        path.write_text(original, encoding='utf-8')
        result = run()
        assert result.returncode == 97 and b'FORBIDDEN_DEVICE_SETUP' in result.stderr and path.read_text(encoding='utf-8') == original
        checks += 1
        result = run('--pool', 'stratum+ssl://private-fixture.test:1234')
        assert result.returncode == 3 and b'FORBIDDEN_DEVICE_SETUP' not in result.stderr and b'private-fixture' not in result.stderr
        assert any(message in result.stderr.lower() for message in (
            b'no mining was started', b'no tcp fallback or mining was started'))
        checks += 1
    print(f'pool security CLI: {checks} refusal/preservation checks passed; no device/network execution')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    main()
