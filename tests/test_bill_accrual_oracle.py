"""Independent exact prefix/settlement oracle; all inputs invented locally, no rates or meter records."""
import decimal
from fractions import Fraction as F
import random
import subprocess
import sys

D = decimal.Decimal
DEN = D(3600000000)
DAY = 86400000


def main():
    rng = random.Random(20261029)
    commands, expected = [], []
    for case in range(1200):
        mode = ('none', 'progressive', 'all')[case % 3]
        elapsed = 3600000 if case < 30 else rng.randrange(2, 4 * DAY)
        cuts = sorted({1, elapsed, elapsed // 2, *[rng.randrange(1, elapsed) for _ in range(3)], *[x for x in (DAY - 1, DAY, DAY + 1) if x < elapsed]})
        watts = D(rng.randrange(0, 10**10)) / 10**5
        threshold = D(rng.randrange(1, 10**6)) / 1000
        r1, r2, base = [D(rng.randrange(-10**8, 10**8)) / 10**7 for _ in range(3)]
        factor = D(rng.randrange(0, 3001)) / 1000
        floor = D(rng.randrange(0, 10000)) / 100 if case % 4 == 0 else None
        cycle, daily = [D(rng.randrange(-1000, 1000)) / 100 for _ in range(2)]
        if case < 30:
            watts = D(0) if case % 5 == 0 else watts
            quantity = watts * elapsed / DEN
            threshold = max(D('0.000000001'), quantity + D((-1, 0, 1)[case % 3]) / 10**9)
        values = (mode, watts, r1, r2, threshold, base, factor, floor if floor is not None else 'none', cycle, daily, elapsed, len(cuts), *cuts)
        commands.append(' '.join(format(value, 'f') if isinstance(value, D) else str(value) for value in values))
        previous = F(0)
        for cut in cuts:
            quantity = F(watts) * cut / int(DEN)
            tier = F(0)
            if mode == 'all':
                tier = quantity * F(r1 if quantity <= threshold else r2)
            elif mode == 'progressive':
                tier = min(quantity, F(threshold)) * F(r1) + max(F(0), quantity - F(threshold)) * F(r2)
            subtotal = quantity * F(base) + tier + F(cycle) + F(daily) * ((cut + DAY - 1) // DAY)
            if cut == elapsed and floor is not None:
                subtotal = max(subtotal, F(floor))
            total = subtotal * F(factor)
            units, rest = divmod(abs(total.numerator) * 10**12, total.denominator)
            units += int(rest * 2 > total.denominator or (rest * 2 == total.denominator and units % 2))
            shown = ('-' if total < 0 and units else '') + f'{units // 10**12}.{units % 10**12:012d}'
            expected.append((total * int(DEN), (total - previous) * int(DEN), F(watts) * cut, shown,
                             str(int(cut != elapsed and (mode == 'all' or floor is not None))), str(int(cut == elapsed))))
            previous = total
    process = subprocess.run([sys.argv[1]], input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=60, check=True)
    lines = process.stdout.splitlines()
    assert len(lines) == len(expected), (len(lines), len(expected))
    for index, (line, wanted) in enumerate(zip(lines, expected)):
        actual = line.split()
        assert len(actual) == 6, index
        for field, value in zip(actual, wanted):
            assert (F(field) == value if isinstance(value, F) else field == value), (index, field, value)
    print(f'bill accrual oracle: {len(commands)} cases / {len(expected)} prefixes / {len(expected) * 6} comparisons passed')


if __name__ == '__main__':
    with decimal.localcontext() as context:
        context.prec = 220
        main()
