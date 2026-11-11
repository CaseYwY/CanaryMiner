"""Actual inert host CLI: diagnostics must exit before config/preferences/platform/device/runtime setup."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from test_config_schema import load, validate


def main():
    executable = str(Path(sys.argv[1]).resolve())
    schema = load('config/diagnostics-v1.schema.json')
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-diagnostic-cli-') as directory:
        root = Path(directory)
        sentinel = b'private-fixture-never-publish'
        originals = {name: b'{malformed-' + sentinel for name in ('config.json', 'language-preferences.json', 'runtime-preferences.json', 'canary.log')}
        for name, data in originals.items():
            (root / name).write_bytes(data)

        def run(*args):
            result = subprocess.run([executable, 'diagnostics', *args], cwd=root, capture_output=True, timeout=8)
            assert sentinel not in result.stdout + result.stderr and b'FORBIDDEN_DEVICE_SETUP' not in result.stderr
            assert all((root / name).read_bytes() == data for name, data in originals.items())
            return result

        previews = {}
        for args, selected in (((), False), (('--preview',), False), (('--include-build',), True), (('--include-build', '--preview'), True)):
            result = run(*args)
            assert result.returncode == 0, result.stderr
            document = json.loads(result.stdout)
            assert validate(document, schema, schema) == []
            assert result.stdout.endswith(b'\n') and b'\r' not in result.stdout and len(result.stdout) <= 1024
            assert document['selection'] == {'build': selected} and ('build' in document) == selected
            digest = hashlib.sha256(result.stdout).hexdigest()
            assert re.search(rb'SHA256 ([0-9a-f]{64})', result.stderr).group(1).decode() == digest
            assert set(p.name for p in root.iterdir()) == set(originals)
            if selected in previews:
                assert previews[selected] == (result.stdout, digest)
            previews[selected] = (result.stdout, digest)
            checks += 7
        for selected in (False, True):
            data, digest = previews[selected]
            flags = ('--include-build',) if selected else ()
            name = 'selected.json' if selected else 'report.json'
            result = run(*flags, '--export', name, '--confirm', digest)
            assert result.returncode == 0 and result.stdout == b'' and (root / name).read_bytes() == data
            result = run(*flags, '--export', name, '--confirm', digest)
            assert result.returncode != 0 and result.stdout == b'' and (root / name).read_bytes() == data
            checks += 2
        digest = previews[False][1]
        for args in (('--export', 'wrong.json', '--confirm', '0' * 64),
                     ('--include-build', '--export', 'wrong.json', '--confirm', digest),
                     ('--export', 'private-fixture-never-publish.json'),
                     ('--export', '../private-fixture-never-publish.json', '--confirm', digest),
                     ('--export', 'con.json', '--confirm', digest),
                     ('--confirm', digest), ('--include-build', '--include-build'),
                     ('--preview', '--export', 'wrong.json', '--confirm', digest),
                     ('--config', str(root / 'config.json')), ('--wallet', sentinel.decode()),
                     ('--password', sentinel.decode()), ('--pool', sentinel.decode()),
                     ('--devices', sentinel.decode()), ('--log-dir', sentinel.decode()),
                     ('--language', sentinel.decode()), ('--bench',), ('--help-all',), ('--unknown', sentinel.decode())):
            result = run(*args)
            assert result.returncode == 2 and result.stdout == b'', (args[0], result.returncode, result.stderr)
            assert not (root / 'wrong.json').exists() and not (root / 'con.json').exists()
            checks += 2
        for args in (('--help',), ('-h',)):
            result = run(*args)
            assert result.returncode == 0 and b'--include-build' in result.stdout and b'never overwrites or uploads' in result.stdout
            checks += 1
        assert set(p.name for p in root.iterdir()) == set(originals) | {'report.json', 'selected.json'}
        # Positive control: this is the actual host-stub miner entry point, not a separate fake diagnostics executable.
        result = subprocess.run([executable], cwd=root, capture_output=True, timeout=8)
        assert result.returncode == 97 and b'FORBIDDEN_DEVICE_SETUP' in result.stderr
        checks += 2
    print(f'diagnostic CLI: {checks} exact-byte/consent/refusal/preservation checks; no device/network execution')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    main()
