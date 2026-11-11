"""Own all ConPTY hosts/jobs and synthetic state. Never uses an installed miner or user's console."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    if os.name != 'nt':
        raise SystemExit('Windows headless ConPTY qualification only')
    probe, cli, supervisor, helper = [Path(value).resolve() for value in sys.argv[1:5]]
    for mode, stop in (('idle', 'q'), ('idle', 'ctrl-c'), ('dashboard', 'q'), ('dashboard', 'ctrl-c'), ('plain-delayed', 'ctrl-c')):
        with tempfile.TemporaryDirectory(prefix='canary-console-owned-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
            root = Path(directory)
            binary = root / 'console-fixture.exe'
            shutil.copy2(cli if mode == 'idle' else supervisor, binary)
            shutil.copy2(helper, root / 'canary_preferences_worker.exe')
            state = root / 'state'
            state.mkdir()
            config = root / 'config.json'
            original = json.dumps({'schema_version': 1, 'state_dir': str(state)}).encode()
            config.write_bytes(original)
            print(mode + '/' + stop, flush=True)
            # A separate state is needed for each healthy/retained run of the same test program.
            subprocess.run([str(probe), str(binary), mode, str(config if mode == 'idle' else state), stop], check=True, timeout=45, creationflags=subprocess.CREATE_NO_WINDOW)
            assert config.read_bytes() == original
            if mode == 'idle':
                assert not tuple(state.iterdir())
            else:
                assert {path.name for path in state.iterdir()} == {'healthy', 'retained'}
                for case in ('healthy', 'retained'):
                    target = state / case
                    assert (target / 'worker-starts').read_bytes().replace(b'\r\n', b'\n') == b'start\n'
                    assert {path.name for path in target.iterdir()} == ({'worker-starts', 'go-output'} if mode == 'plain-delayed' else {'worker-starts'})
    print('Windows actual idle/supervisor: ten healthy/retained-output Q/Ctrl+C cases; original state preserved, owned jobs settled')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    main()
