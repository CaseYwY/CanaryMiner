"""Independent Fraction/sorted-original oracle, entirely synthetic; no FPS tool/real-game/source qualification."""
from fractions import Fraction
import json
from pathlib import Path
import random
import subprocess
import sys


def main():
    executable = str(Path(sys.argv[1]).resolve())
    generator = random.Random(471902)
    lines, expected = ['200'], []

    def frames(n, span):
        values = [generator.randrange(10000, 5000000) for _ in range(n)]
        total = sum(values)
        values = [value * span // total for value in values]
        values[-1] += span - sum(values)
        return values

    def metrics(values):
        ordered = sorted(values)
        tail = (len(values) + 99) // 100
        rank = (len(values) * 99 + 99) // 100
        return [len(values), sum(values), tail, sum(ordered[-tail:]), ordered[rank - 1], ordered[-1]]

    def rate(m, low=False):
        return Fraction(m[2] * 10**12, m[3]) if low else Fraction(m[0] * 10**12, m[1])

    def loss(baseline, candidate):
        exact = max(Fraction(0), (1 - candidate / baseline) * 10000)
        ceiling = (exact.numerator + exact.denominator - 1) // exact.denominator
        return max(0, min(10000, ceiling + generator.choice((-1, 0, 1))))

    for _ in range(200):
        span = generator.randrange(500000000, 5000000001)
        baseline, candidate = frames(generator.randrange(100, 601), span), frames(generator.randrange(100, 601), span)
        b, c = metrics(baseline), metrics(candidate)
        mean_floor = max(1, int(rate(c)) + generator.choice((-1, 0, 1)))
        low_floor = min(mean_floor, max(1, int(rate(c, True)) + generator.choice((-1, 0, 1))))
        mean_loss, low_loss = loss(rate(b), rate(c)), loss(rate(b, True), rate(c, True))
        maximum = max(b[5], c[5]) + generator.choice((-1, 0, 1))
        p99 = min(maximum, max(b[4], c[4]) + generator.choice((-1, 0, 1)))
        passed = all(rate(m) >= mean_floor and rate(m, True) >= low_floor and m[5] <= maximum and m[4] <= p99 for m in (b, c))
        passed = passed and rate(c) * 10000 >= rate(b) * (10000 - mean_loss) and rate(c, True) * 10000 >= rate(b, True) * (10000 - low_loss)
        lines.append(' '.join(map(str, [mean_floor, low_floor, mean_loss, low_loss, maximum, p99, len(baseline), len(candidate), *baseline, *candidate])))
        expected.append([int(passed), *b, *c])
    result = subprocess.run([executable], input=('\n'.join(lines) + '\n').encode(), capture_output=True, timeout=30)
    assert result.returncode == 0, result.stderr.decode(errors='replace')
    actual = [json.loads(line) for line in result.stdout.splitlines()]
    assert len(actual) == len(expected)
    for index, (observed, wanted) in enumerate(zip(actual, expected)):
        assert observed == wanted, (index, observed, wanted)
    assert any(row[0] for row in actual) and any(not row[0] for row in actual)
    print('game experience oracle: 200 paired cases / 2600 exact metric-and-decision comparisons passed')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    main()
