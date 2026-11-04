"""Real host-stub CLI reaches an explicit device sentinel only; never a GPU, public pool or worker."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    executable = str(Path(sys.argv[1]).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-language-cli-') as directory:
        root = Path(directory)
        config = root / 'config.json'
        config.write_text(json.dumps({'schema_version': 1, 'state_dir': str(root)}), encoding='utf-8')
        path = root / 'language-preferences.json'
        def run(*args):
            return subprocess.run([executable, *args, '--config', str(config)], capture_output=True, timeout=8)
        result = run()
        assert result.returncode == 97 and b'FORBIDDEN_DEVICE_SETUP' in result.stderr and not path.exists()
        checks += 1
        request = dict(schema_version=1, language='fr', formatting_locale='ar-EG', digits='locale_default', text_order='visual',
                       direction='rtl', ambiguous_cells=2, emoji_cells=1, grouping=False)
        for original in (json.dumps(request), '{"schema_version":99,"private-fixture":"not-for-output"}', '[]', 'x' * 4097):
            path.write_text(original, encoding='utf-8')
            for args in ((), ('--language=zh-Hans', '--format-locale', 'en-US', '--ascii-ui')):
                result = run(*args)
                assert result.returncode == 97 and b'FORBIDDEN_DEVICE_SETUP' in result.stderr
                assert (b'effective UI en / legacy per-field formatting' if args or original == json.dumps(request)
                        else b'legacy English presentation retained') in result.stderr
                assert b'private-fixture' not in result.stdout + result.stderr and b'not-for-output' not in result.stdout + result.stderr
                assert path.read_text(encoding='utf-8') == original and not (root / 'runtime-preferences.json').exists()
                if original != json.dumps(request):
                    assert b'original file preserved' in result.stderr
                checks += 4
        for args in (('--language',), ('--language=private-fixture',), ('--format-locale', 'private-fixture'), ('--language=en', '--language=fr'),
                     ('--format-locale=en', '--format-locale=de'), ('--ascii-ui', '--ascii-ui'), ('--ascii-ui=private-fixture',)):
            result = run(*args)
            assert result.returncode == 2 and b'FORBIDDEN_DEVICE_SETUP' not in result.stderr and b'private-fixture' not in result.stderr
            assert path.read_text(encoding='utf-8') == original
            checks += 2
        for args in (('version',), ('settings', '--help'), ('--help',), ('--bench', '--help')):
            result = run(*args, '--language=ar', '--format-locale=de-DE', '--ascii-ui')
            assert result.returncode == 0 and b'effective UI en / legacy per-field formatting' in result.stderr
            assert path.read_text(encoding='utf-8') == original and b'FORBIDDEN_DEVICE_SETUP' not in result.stderr
            checks += 2
        # The recovery option itself may repeat across new invocations but never rewrites saved requests.
        result = run('settings', '--ascii-ui')
        assert result.returncode == 2 and b'requires an interactive terminal' in result.stderr and path.read_text(encoding='utf-8') == original
        checks += 1
    print(f'language CLI: {checks} fallback/refusal/preservation checks; no device/network execution')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    main()
