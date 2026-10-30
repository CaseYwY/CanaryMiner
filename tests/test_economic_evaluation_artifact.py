"""Check the CPU evaluator's production artifacts exclude its blocking test hook."""
from pathlib import Path
import sys


def main():
    ordinary, controlled, library = (Path(p).read_bytes() for p in sys.argv[1:4])
    hook = b'economicEvaluationTestBuild'
    assert hook not in ordinary, 'ordinary CPU executable contains the evaluator test hook'
    assert hook not in library, 'production optimizer library contains the evaluator test hook'
    assert b'synthetic computation failure' in controlled, 'controlled fixture did not retain its injected failure'
    checks = 3
    if len(sys.argv) > 4:
        guard, guard_controlled = (Path(p).read_bytes() for p in sys.argv[4:6])
        assert hook not in guard, 'ordinary reference guard contains the evaluator test hook'
        assert b'synthetic guard computation barrier' in guard_controlled, 'controlled guard did not retain its computation barrier'
        checks += 2
    print(f'economic evaluation artifacts: {checks} production/fixture isolation checks passed')


if __name__ == '__main__':
    main()
