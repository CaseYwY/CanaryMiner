"""Independent Fraction oracle: dimensional units and exact ordering, no production imports."""
from fractions import Fraction as F
from pathlib import Path
import random
import subprocess
import sys


def main():
    rng = random.Random(5910)
    rows, expected = [], []
    for _ in range(192):
        work = [F(rng.randrange(0, 10000), 100) for _ in range(2)]
        duration = [rng.randrange(100, 6000) for _ in range(2)]
        mw = [rng.randrange(0, 400001) for _ in range(2)]
        credit, price, fx = F(rng.randrange(0, 100), 10), F(rng.randrange(1, 500), 1000), F(rng.randrange(75, 126), 100)
        tariff, tariff_fx, haircut = F(rng.randrange(-20, 101), 100), F(rng.randrange(50, 151), 100), F(rng.randrange(0, 101), 100)
        horizon = rng.randrange(120000, 7200001)
        transition = [rng.randrange(0, 10001) for _ in range(2)]
        # Explicit future transition energy is independent of the measured run's mean power.
        overhead = [F(t * rng.randrange(0, 400001), 1000) for t in transition]
        repeat = [rng.randrange(2) for _ in range(2)]
        test_ms = sum(duration) + 1000
        test_watt_ms = F(mw[0], 1000) * (duration[0] + 1000) + F(mw[1], 1000) * duration[1]
        candidates = []
        for i in range(2):
            run = horizon - transition[i] - repeat[i] * test_ms
            tokens = credit * work[i] * F(run, duration[i]) / 10000
            energy = (F(mw[i], 1000) * run + overhead[i] + repeat[i] * test_watt_ms) / 3600000000
            revenue = tokens * price * fx * (1 - haircut)
            cost = energy * tariff * tariff_fx
            net = revenue - cost
            candidates.append([tokens, energy, revenue, cost, net, net * F(1000, horizon)])
        best = max(range(2), key=lambda i: (candidates[i][-1], -i))
        if candidates[best][-1] <= 0:
            best = -1
        expected.append((best, [n for c in candidates for n in c]))

        def decimal(n):
            # All generated input rationals have denominators dividing 1000.
            value = n * 1000
            assert value.denominator == 1
            signed = value.numerator
            return ('-' if signed < 0 else '') + str(abs(signed) // 1000) + '.' + str(abs(signed) % 1000).zfill(3)

        values = [*map(decimal, work), *duration, *mw, decimal(credit), decimal(price), decimal(fx), decimal(tariff), decimal(tariff_fx), horizon, decimal(haircut), transition[0], decimal(overhead[0]), transition[1], decimal(overhead[1]), *repeat]
        rows.append(' '.join(map(str, values)))
    output = subprocess.run([str(Path(sys.argv[1]).resolve(strict=True))], input='\n'.join(rows)+'\n', text=True, capture_output=True, check=True, timeout=90).stdout.splitlines()
    assert len(output) == len(expected)
    comparisons = 0
    for i, (line, (best, values)) in enumerate(zip(output, expected)):
        fields = line.split()
        assert len(fields) == 25, (i, line)
        assert int(fields[0]) == best, (i, 'best', line, best)
        actual = [F(fields[j]) / F(fields[j+1]) for j in range(1, 25, 2)]
        assert actual == values, (i, actual, values)
        comparisons += len(values) + 1
    print(f'economic Fraction oracle: {len(rows)} cases / {comparisons} exact value/ordering comparisons')


if __name__ == '__main__':
    main()
