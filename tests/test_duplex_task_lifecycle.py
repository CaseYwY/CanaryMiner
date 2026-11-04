"""Only kill the parent we just spawned; observe its exact owned helper identity, never other applications."""
import argparse
import os
from pathlib import Path
import queue
import subprocess
import threading
from test_forecast_lifecycle import process_identity, wait


def main(args):
    env = dict(os.environ, CANARY_SYNTHETIC_SECRET='fixture-only', SSLKEYLOGFILE='fixture-only', HTTPS_PROXY='fixture-only')
    parent = subprocess.Popen([str(args.parent), str(args.fixture)], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env)
    lines = queue.Queue()
    reader = threading.Thread(target=lambda: lines.put(parent.stdout.readline()), daemon=True)
    reader.start()
    try:
        pid = int(lines.get(timeout=10))
        alive, close = process_identity(pid)
        try:
            assert alive(), 'owned helper did not reach stalled phase'
            parent.kill()
            parent.wait(timeout=5)
            assert wait(lambda: not alive()), 'duplex helper outlived its exact parent'
        finally:
            close()
    finally:
        if parent.poll() is None:
            parent.kill()
        parent.wait(timeout=5)
        reader.join(timeout=5)
        parent.stdout.close()
    print('duplex lifecycle: true owned-parent death and minimized environment passed')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    parser = argparse.ArgumentParser()
    parser.add_argument('--parent', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    main(parser.parse_args())
