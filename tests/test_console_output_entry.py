"""Ordinary supervisor admission and output-failure paths; no GPU or public/local endpoint is contacted."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time


def main():
    fixture, helper = [Path(value).resolve() for value in sys.argv[1:3]]
    flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
    cases = ['missing-helper', 'broken-startup-output'] + ([] if os.name == 'nt' else ['lost-output-during-work'])
    for mode in cases:
        with tempfile.TemporaryDirectory(prefix='canary-console-entry-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
            root = Path(directory)
            binary = root / ('owned-fixture.exe' if os.name == 'nt' else 'owned-fixture')
            sibling = root / helper.name
            shutil.copy2(fixture, binary)
            state = root / 'state'
            state.mkdir()
            environment = dict(os.environ)
            if mode == 'missing-helper':
                decoy = root / 'path-decoy'
                decoy.mkdir()
                shutil.copy2(helper, decoy / helper.name)
                environment['PATH'] = str(decoy) + os.pathsep + environment.get('PATH', '')
                result = subprocess.run([str(binary), 'plain', str(state)], stdin=subprocess.DEVNULL, capture_output=True, env=environment, timeout=8, creationflags=flags)
                assert result.returncode == 8 and result.stdout == b'' and not tuple(state.iterdir()), mode
            else:
                shutil.copy2(helper, sibling)
                if mode == 'broken-startup-output':
                    read, write = os.pipe()
                    os.close(read)
                    try:
                        child = subprocess.Popen([str(binary), 'plain', str(state)], stdin=subprocess.DEVNULL, stdout=write, stderr=subprocess.PIPE, creationflags=flags)
                    finally:
                        os.close(write)
                else:
                    child = subprocess.Popen([str(binary), 'plain-delayed', str(state)], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                try:
                    if mode == 'lost-output-during-work':
                        deadline = time.monotonic() + 5
                        while not (state / 'worker-starts').exists():
                            assert child.poll() is None and time.monotonic() < deadline
                            time.sleep(.01)
                        child.stdout.close()  # trigger a real helper write failure only after ordinary worker admission
                        (state / 'go-output').write_text('go\n')
                    child.wait(timeout=8)
                    assert child.returncode == 8, mode
                    if mode == 'broken-startup-output':
                        assert not tuple(state.iterdir()), 'startup output was not accepted, but a worker started'
                    else:
                        assert (state / 'worker-starts').read_bytes() == b'start\n'
                        assert {path.name for path in state.iterdir()} == {'worker-starts', 'go-output'}
                finally:
                    if child.poll() is None:
                        child.kill()
                        child.wait(timeout=3)
                    if child.stdout is not None:
                        child.stdout.close()
                    child.stderr.close()
            print(mode + ': PASS (latched refusal, no fallback or worker restart)', flush=True)
    print(f'console ordinary entry: {len(cases)} owned CPU failure cases passed; Windows active work is separately tested inside owned ConPTY')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    main()
