"""Actual inert CLI/genuine sibling and owned malformed originals. Never a real miner, device or configuration."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    cli, helper = [Path(value).resolve() for value in sys.argv[1:3]]
    expected = b'config file rejected: invalid or unsupported config-v1 (schema_version 1 required); private details omitted; original preserved.\n'
    original = 'fixture.private-config-original'
    cases = {
        'unknown-key': {'schema_version': 1, original: 'unchanged'},
        'unknown-control-key': {'schema_version': 1, original+'\x1b[2J': 'unchanged'},
        'pool-original': {'schema_version': 1, 'pools': [original+'@pool.invalid']},
        'compression-original': {'schema_version': 1, 'pools': [{'url': 'fixture.invalid:3333', 'compression': original}]},
        'network-original': {'schema_version': 1, 'pools': [{'url': 'fixture.invalid:3333', 'network': original}]},
        'semantics-original': {'schema_version': 1, 'pools': [{'url': 'fixture.invalid:3333', 'target_semantics': original}]},
        'api-unknown': {'schema_version': 1, 'api': {original: 'unchanged'}},
        'log-unknown': {'schema_version': 1, 'log': {original: 'unchanged'}},
        'shutdown-unknown': {'schema_version': 1, 'shutdown': {original: 'unchanged'}},
        'tuning-unknown': {'schema_version': 1, 'tuning': {original: 'unchanged'}},
        'pool-unknown': {'schema_version': 1, 'pools': [{'url': 'fixture.invalid:3333', original: 'unchanged'}]},
        'unsupported-schema': {'schema_version': 99, 'worker': original},
        'missing-schema': {'worker': original},
        'wrong-type': {'schema_version': 1, 'worker': [original]},
        'wrong-root': [original],
    }
    documents = {name: json.dumps(value).encode() for name, value in cases.items()}
    documents.update({'empty': b'', 'malformed': b'{'+original.encode(),
                      'invalid-utf8-key': b'{"schema_version":1,"fixture-private-\xff":0}',
                      'nul-tail': b'{"schema_version":1}\0'+original.encode()})
    failures = []
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-config-errors-') as directory:
        root = Path(directory)
        binary = root/('config-cli'+('.exe' if os.name == 'nt' else ''))
        shutil.copy2(cli, binary)
        shutil.copy2(helper, root/('canary_preferences_worker'+('.exe' if os.name == 'nt' else '')))
        config = root/'fixture-private-config.json'
        unrelated = root/'unrelated.txt'
        unrelated.write_bytes(b'fixture-original-preserved')

        def invoke():
            return subprocess.run([str(binary), '--config', str(config)], cwd=root, capture_output=True, timeout=15)

        for name, document in documents.items():
            config.write_bytes(document)
            result = invoke()
            output = (result.stdout+result.stderr).replace(b'\r\n', b'\n')
            assert result.returncode == 3 and b'FORBIDDEN_DEVICE_SETUP' not in output, name
            assert config.read_bytes() == document and unrelated.read_bytes() == b'fixture-original-preserved', name
            if output != expected or original.encode() in output or b'\x1b' in output or str(config).encode() in output:
                failures.append(name)
            checks += 1
        # Structural parsing and final field validation are separate existing phases. Do not change either to fit display tests.
        invalid_worker = b'{"schema_version":1,"worker":"\xff"}'
        config.write_bytes(invalid_worker)
        result = invoke()
        assert result.returncode == 3 and (result.stdout+result.stderr).replace(b'\r\n', b'\n') == b"worker may contain letters, digits, '_', '-' and '.' only\n", 'unchanged-worker-validation'
        assert config.read_bytes() == invalid_worker and unrelated.read_bytes() == b'fixture-original-preserved'
        checks += 1
        prefix = json.dumps({'schema_version': 1, 'state_dir': str(root/'state')}).encode()
        oversized = prefix+b' '*((1 << 20)+1-len(prefix))
        config.write_bytes(oversized)
        result = invoke()
        assert result.returncode == 3 and (result.stdout+result.stderr).replace(b'\r\n', b'\n') == b'config exceeds size limit; original preserved\n', 'oversized'
        assert config.read_bytes() == oversized and not (root/'state').exists()
        checks += 1
        config.unlink()
        result = invoke()
        assert result.returncode == 3 and (result.stdout+result.stderr).replace(b'\r\n', b'\n') == b'cannot read config file; original preserved\n', 'missing'
        assert not config.exists() and unrelated.read_bytes() == b'fixture-original-preserved'
        checks += 1
        config.write_bytes(prefix)
        result = invoke()
        assert result.returncode == 97 and b'FORBIDDEN_DEVICE_SETUP' in result.stderr, 'valid config must still reach the inert admission sentinel'
        assert config.read_bytes() == prefix and not (root/'state').exists()
        checks += 1
    assert not failures, 'Private config rejection envelope failed in: '+', '.join(failures) # case IDs only, never private output bytes
    print('config_error_privacy_cli:', checks, 'exact safe rejection/read/size/positive-admission cases; originals preserved, no real device or network')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    main()
