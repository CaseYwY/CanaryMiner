"""Check the CPU evaluator's production artifacts exclude its blocking test hook."""
from pathlib import Path
import sys


def main():
    ordinary, controlled, library = (Path(p).read_bytes() for p in sys.argv[1:4])
    hook = b'economicEvaluationTestBuild'
    assert hook not in ordinary, 'ordinary CPU executable contains the evaluator test hook'
    assert hook not in library, 'production optimizer library contains the evaluator test hook'
    assert b'synthetic computation failure' in controlled, 'controlled fixture did not retain its injected failure'
    print('economic evaluation artifacts: 3 production/fixture isolation checks passed')


if __name__ == '__main__':
    main()
