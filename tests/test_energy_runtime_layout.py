"""Private CPU-only sibling-layout probes; no mining, network or device discovery."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    probe, host, authorization, collector, ordinary, library = [Path(p).resolve() for p in sys.argv[1:7]]
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-runtime-layout-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        install = root / 'synthetic-\u80fd\u8017 space' / 'bin'
        work = root / 'different working directory'
        install.mkdir(parents=True)
        work.mkdir()
        for source in (probe, host, authorization, collector):
            shutil.copy2(source, install / source.name)
        # A genuine helper in CWD/PATH must never substitute for a missing installed sibling.
        shutil.copy2(authorization, work / authorization.name)
        env = dict(os.environ, PATH=str(work) + os.pathsep + os.environ.get('PATH', ''))

        def run(expected):
            nonlocal checks
            result = subprocess.run([str(install / probe.name), '--native-probe'], cwd=work, env=env,
                                    capture_output=True, timeout=10)
            assert result.returncode == 0 and expected in result.stdout, 'sibling probe result mismatch'
            assert not (work / 'state').exists(), 'default-off read created state'
            checks += 2

        run(b'native sibling ready; persistence disabled')
        (install / authorization.name).unlink()
        run(b'native sibling unavailable')
        shutil.copy2(collector, install / authorization.name)
        run(b'native sibling unavailable')
        shutil.copy2(authorization, install / authorization.name)
        run(b'native sibling ready; persistence disabled')
        for binary in (install / host.name, ordinary):
            result = subprocess.run([str(binary), 'settings'], cwd=work, capture_output=True, timeout=5)
            assert result.returncode == 2 and b'interactive terminal' in result.stderr
            assert not (work / 'state').exists()
            checks += 2
        # The ordinary entry point has no reference to the development factory; native helpers have no test hooks.
        assert b'makeNativeEnergyRuntime' not in ordinary.read_bytes()
        checks += 1
        for binary in (authorization, collector, library):
            data = binary.read_bytes()
            for marker in (b'fixture-lease-no-ack.json', b'fixture-guard-delay.json', b'fixture-parent-death.json'):
                assert marker not in data, 'fixture hook leaked into native artifact'
                checks += 1
        if len(sys.argv) > 7:
            build, cmake, config = sys.argv[7:10]
            prefix = root / 'component install'
            result = subprocess.run([cmake, '--install', build, '--prefix', str(prefix), '--component',
                                     'energy-runtime-host', '--config', config or 'Release'], capture_output=True, timeout=20)
            assert result.returncode == 0, 'development component install failed'
            assert {p.name for p in (prefix / 'bin').iterdir()} == {host.name, authorization.name, collector.name}
            checks += 2
            install = prefix / 'bin'
            shutil.copy2(probe, install / probe.name)
            run(b'native sibling ready; persistence disabled')
    print(f'energy runtime layout: {checks} checks passed')


if __name__ == '__main__':
    main()
