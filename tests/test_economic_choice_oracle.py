"""Independent Fraction model of exact resume/switch thresholds; no production imports."""
from collections import Counter
from fractions import Fraction as F
from pathlib import Path
import random
import subprocess
import sys


def main():
    rng = random.Random(7710)
    rows, expected = [], []
    # Enum observations from the probe; the model below computes decisions in rational arithmetic.
    states = dict(unavailable=2, nonpositive=4, ineligible=5, margin=6, hold=7, resume=8, switch=9)
    counts = Counter()
    for i in range(384):
        work = [rng.randrange(0, 1000) for _ in range(2)]
        ms = [rng.randrange(100, 9000) for _ in range(2)]
        mw = [rng.randrange(0, 400001) for _ in range(2)]
        credit = rng.randrange(0, 31)
        price = F(rng.randrange(1, 31), 1000)
        tariff = F(rng.randrange(-20, 101), 100)
        resume, gain = [F(rng.randrange(0, 1001), 1000000) for _ in range(2)]
        fraction = F(rng.randrange(0, 101), 100)
        if i % 3 == 0:
            resume = gain = fraction = F(0)
        current = [-2, -1, 0, 10, 11][i % 5]
        rate = [F(credit * work[j], 10 * ms[j]) * price - F(mw[j], 3600000000) * tariff for j in range(2)]
        best = max(range(2), key=lambda j: (rate[j], -j))
        point, name = -1, 'unavailable'
        if current == 11:
            name = 'ineligible'
        elif current != -2:
            active = None if current == -1 else [0, 10].index(current)
            if rate[best] <= 0 or (active is not None and rate[active] <= 0):
                name = 'nonpositive'
            elif active is None:
                name = 'resume' if rate[best] >= resume else 'margin'
                point = best if name == 'resume' else -1
            elif best == active or rate[best] - rate[active] <= 0 or rate[best] - rate[active] < max(gain, fraction * rate[active]):
                name, point = 'hold', active
            else:
                name, point = 'switch', best
        counts[name] += 1
        expected.append((states[name], point, -1 if point < 0 else [0, 10][point]))

        def decimal(value):
            scaled = F(value) * 1000000
            assert scaled.denominator == 1
            n = scaled.numerator
            return ('-' if n < 0 else '') + str(abs(n) // 1000000) + '.' + str(abs(n) % 1000000).zfill(6)

        rows.append(' '.join(map(str, [*work, *ms, *mw, credit, decimal(price), decimal(tariff), decimal(resume), decimal(gain), decimal(fraction), current])))
    output = subprocess.run([str(Path(sys.argv[1]).resolve(strict=True))], input='\n'.join(rows)+'\n', text=True, capture_output=True, check=True, timeout=90).stdout.splitlines()
    assert len(output) == len(expected)
    for i, (row, wanted) in enumerate(zip(output, expected)):
        assert tuple(map(int, row.split())) == wanted, (i, row, wanted)
    assert set(counts) == set(states), counts
    print(f'economic choice Fraction oracle: {len(rows)} cases / {len(rows)*3} exact state/point/wait comparisons; all seven decisions exercised')


if __name__ == '__main__':
    main()
