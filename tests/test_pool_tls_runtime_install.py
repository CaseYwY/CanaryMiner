"""Install the CPU fixture and its genuine helpers; no GPU, TLS connection or trust-store change."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()


def main(args):
    artifacts = [args.cli.resolve(), args.preferences.resolve(), args.tls_helper.resolve()]
    expected = {path.name: digest(path) for path in artifacts}
    assert len(expected) == 3, 'the installation requires three distinct artifacts'
    checks = 0
    # Other host components retain their existing default-install behavior. Check the
    # generated guards for this component without installing those unrelated fixtures.
    guards = [line.strip() for path in args.build.rglob('cmake_install.cmake')
              for line in path.read_text(encoding='utf-8').splitlines()
              if line.strip().startswith('if(') and '"pool-tls-runtime-host"' in line]
    assert guards and all(line == 'if(CMAKE_INSTALL_COMPONENT STREQUAL "pool-tls-runtime-host")'
                          for line in guards), 'TLS host fixtures must require an explicit install component'
    checks += 1
    with tempfile.TemporaryDirectory(prefix='canary-tls-install-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        prefix = root / 'local \u5b89\u88c5 with spaces'
        cwd = root / 'unrelated working directory'
        cwd.mkdir()
        result = subprocess.run([str(args.cmake), '--install', str(args.build), '--prefix', str(prefix),
                                 '--component', 'pool-tls-runtime-host', '--config', args.config or 'Release'],
                                capture_output=True, timeout=25)
        assert result.returncode == 0, 'host component install failed'
        installed = prefix / 'bin'
        assert {path.name for path in prefix.iterdir()} == {'bin'}, 'unexpected install directory'
        assert {path.name for path in installed.iterdir()} == set(expected), 'incomplete or extra installed artifact'
        for path in artifacts:
            target = installed / path.name
            assert target.is_file() and not target.is_symlink(), 'expected a regular installed file'
            assert digest(target) == expected[path.name], 'installed bytes differ from the selected build'
            assert os.access(target, os.X_OK), 'installed artifact is not executable'
            checks += 3
        checks += 3

        # This is a bounded artifact marker check, not a complete native dependency audit.
        tls_bytes = (installed / args.tls_helper.name).read_bytes().lower()
        for marker in (b'nvcuda.dll', b'libcuda.so', b'nvml.dll', b'libnvidia-ml.so', b'cuinit', b'cudagetdevicecount'):
            assert marker not in tls_bytes, 'GPU loader marker present in the CPU TLS helper'
            checks += 1

        config = root / 'config.json'
        config.write_text(json.dumps(dict(schema_version=1, state_dir=str(root), donation=False,
                                         wallet='prl1' + 'q' * 59,
                                         pools=['stratum+ssl://pool-fixture.invalid:443'])), encoding='utf-8')
        original = config.read_bytes()
        # A genuine helper in both CWD and PATH cannot replace a missing installed sibling.
        shutil.copy2(args.preferences, cwd / args.preferences.name)
        env = {key: value for key, value in os.environ.items() if not key.startswith('CANARY_PEARL_')}
        env['PATH'] = str(cwd) + os.pathsep + env.get('PATH', '')
        executable = installed / args.cli.name

        def launch(expected_code):
            nonlocal checks
            result = subprocess.run([str(executable), '--algo', 'pearl-pow', '--config', str(config)],
                                    cwd=cwd, env=env, capture_output=True, timeout=15)
            output = result.stdout + result.stderr
            assert result.returncode == expected_code, 'installed CLI admission result mismatch'
            assert (b'FORBIDDEN_DEVICE_SETUP' in output) == (expected_code == 97), 'device admission boundary changed'
            assert b'pool-fixture.invalid' not in output, 'private endpoint was echoed'
            assert config.read_bytes() == original, 'read-only installed startup changed configuration'
            assert {path.name for path in cwd.iterdir()} == {args.preferences.name}, 'startup wrote into unrelated CWD'
            checks += 5

        launch(97)  # The inert entry stops before any actual device/source setup.
        preferences = installed / args.preferences.name
        preferences.unlink()
        launch(3)
        shutil.copy2(args.preferences, preferences)
        launch(97)
        assert {path.name: digest(path) for path in installed.iterdir()} == expected, 'restored install differs'
        checks += 1

    # Installing a developer build must not open the separate production-release gate.
    verifier_path = Path(__file__).resolve().parents[1] / 'packaging/release/verify_artifacts.py'
    spec = importlib.util.spec_from_file_location('tls_install_release_verifier', verifier_path)
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    with tempfile.TemporaryDirectory(prefix='canary-tls-release-control-') as directory:
        cache = Path(directory) / 'CMakeCache.txt'
        minimal = ('CANARY_ENABLE_ENERGY_RUNTIME:BOOL=OFF\n'
                   'CANARY_ENABLE_WORKER_CONTROL_RUNTIME:BOOL=OFF\n'
                   'CANARY_FEE_TEST_POLICY:BOOL=OFF\n'
                   'CANARY_ENABLE_POOL_TLS_RUNTIME:BOOL=OFF\n')
        cache.write_text(minimal, encoding='utf-8')
        verifier.check_cache(cache)
        checks += 1
        cache.write_text(minimal.replace('CANARY_ENABLE_POOL_TLS_RUNTIME:BOOL=OFF',
                                         'CANARY_ENABLE_POOL_TLS_RUNTIME:BOOL=ON'), encoding='utf-8')
        try:
            verifier.check_cache(cache)
        except ValueError:
            checks += 1
        else:
            raise AssertionError('TLS capability alone did not close the release gate')
    assert (args.build / 'CMakeCache.txt').read_text(encoding='utf-8').splitlines().count(
        'CANARY_ENABLE_POOL_TLS_RUNTIME:BOOL=ON') == 1, 'expected a TLS-enabled host build'
    try:
        verifier.check_cache(args.build / 'CMakeCache.txt')
    except ValueError:
        checks += 1
    else:
        raise AssertionError('TLS-enabled developer cache was accepted for release')
    print(f'pool TLS host install: {checks} layout, byte identity, isolated startup and release-boundary checks passed')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'cmake', 'cli', 'preferences', 'tls-helper'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--config', default='Release')
    main(parser.parse_args())
