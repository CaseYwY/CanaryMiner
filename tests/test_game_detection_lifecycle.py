"""Readonly native observation of owned synthetic targets, plus parent-bound helper death."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
from test_forecast_lifecycle import process_identity, wait


def main():
    if os.name != 'nt' and '--inside-pid-namespace' not in sys.argv:
        unshare = shutil.which('unshare')
        if not unshare:
            print('SKIP native lifecycle: isolated PID namespace tool unavailable')
            return 77
        result = subprocess.run([unshare, '--user', '--map-root-user', '--pid', '--fork', '--mount-proc', sys.executable, '-B', str(Path(__file__).resolve()), *sys.argv[1:], '--inside-pid-namespace'], capture_output=True, text=True, timeout=100)
        if result.returncode and 'unshare failed: Operation not permitted' in result.stderr:
            print('SKIP native lifecycle: isolated PID namespace permission unavailable')
            return 77
        print(result.stdout, end='')
        if result.returncode:
            print(result.stderr, end='', file=sys.stderr)
        return result.returncode
    probe, fixture, native, library = [Path(p).resolve(strict=True) for p in sys.argv[1:5]]
    checks = 0

    def check(ok):
        nonlocal checks
        assert ok
        checks += 1

    with tempfile.TemporaryDirectory(prefix='canary-game-detection-', dir=os.environ.get('CANARY_TEST_TMP')) as directory:
        root = Path(directory)
        suffix = '.exe' if os.name == 'nt' else ''
        target = root / ('fixture_game_target' + suffix)
        similar = root / ('fixture_game_target_extra' + suffix)
        stalled = root / ('fixture_game_stalled' + suffix)
        for path in (target, similar, stalled):
            shutil.copy2(fixture, path)
        children = []

        def launch(path):
            p = subprocess.Popen([str(path), 'owned-target'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            children.append(p)
            check(p.stdout.readline().strip() == b'ready')
            return p

        def finish(p):
            if p.poll() is None:
                p.kill()
            p.wait(timeout=5)
            p.stdout.close()
            p.stderr.close()

        scope = '-'

        def observation():
            nonlocal scope
            # A changing process table is honestly incomplete. Bound the wait for one complete native observation.
            for _ in range(8):
                result = subprocess.run([str(probe), str(native), 'once', target.name, scope], capture_output=True, text=True, timeout=5)
                check(result.returncode == 0)
                status, matches, returned_scope, digest = result.stdout.split()
                if status == '0':
                    check(scope == '-' or returned_scope == scope)
                    scope = returned_scope
                    return int(matches), digest
                check(status in ('3', '4') and matches == '0' and digest == '-')
                time.sleep(0.05)
            raise AssertionError('no complete bounded native observation')

        try:
            unrelated = launch(similar)
            check(observation() == (0, '-'))
            first = launch(target)
            count, identity = observation()
            check(count == 1 and identity != '-')
            check(observation() == (count, identity))
            second = launch(target)
            count, pair = observation()
            check(count == 2 and pair != identity)
            finish(second)
            check(observation() == (1, identity))
            finish(first)
            check(observation() == (0, '-'))
            third = launch(target)
            count, renewed = observation()
            check(count == 1 and renewed != identity)  # a new creation identity, not a durable PID/name-only identity
            finish(third)
            check(unrelated.poll() is None)
            parent = subprocess.Popen([str(probe), str(stalled), 'life'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            children.append(parent)
            marker = root / 'fixture-game-ready'
            check(wait(lambda: marker.exists() and marker.read_text().strip().isdigit()))
            alive, close = process_identity(int(marker.read_text()))
            try:
                check(alive() and parent.poll() is None)
                finish(parent)
                check(wait(lambda: not alive()))
            finally:
                close()
        finally:
            for child in children:
                finish(child)
    for artifact in (native, library):
        data = artifact.read_bytes()
        for marker in (b'fixture-game-ready', b'gameDetectionTestIdentity', b'gameDetectionTestName'):
            check(marker not in data)
    print(f'game detection lifecycle: {checks} checks; owned target identity, exact-name scope and helper parent death')


if __name__ == '__main__':
    sys.exit(main())
