"""A positive fixture marker must be absent from the separately compiled ordinary run loop."""
from pathlib import Path
import sys


def main():
    ordinary, fixture = (Path(p).read_bytes() for p in sys.argv[1:3])
    marker = b'workPauseTestSource'
    assert marker in fixture, 'fixture marker must be observable before testing absence'
    assert marker not in ordinary, 'test source/telemetry bypass leaked into ordinary run loop'
    print('work pause artifact: fixture seam isolated from ordinary run loop')


if __name__ == '__main__':
    main()
