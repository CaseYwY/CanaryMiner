"""Seeded exact arithmetic against Python Decimal; synthetic numbers, no library dependency or internet."""
import argparse
from decimal import Decimal, localcontext, ROUND_HALF_EVEN
import random
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe")
    args = parser.parse_args()
    randomizer = random.Random(20261028)
    rows, expected = [], []
    with localcontext() as context:
        context.prec = 200
        for _ in range(1500):
            values = []
            for _ in range(2):
                n = randomizer.randrange(-(10 ** randomizer.randrange(1, 39)), 10 ** randomizer.randrange(1, 39))
                scale = randomizer.randrange(19)
                values.append(Decimal(n).scaleb(-scale))
            a, b = values
            denominator = randomizer.choice([1, 3, 7, 60, 480, 719, 2**32-1])
            places = randomizer.randrange(25)
            rows.append(f"{a:f} {b:f} {denominator} {places}\n")
            expected.append((a + b, a - b, a * b, (a > b) - (a < b), (a / denominator).quantize(Decimal(1).scaleb(-places), rounding=ROUND_HALF_EVEN),
                             (a / (abs(b) or Decimal(1))).quantize(Decimal(1).scaleb(-places), rounding=ROUND_HALF_EVEN)))
    result = subprocess.run([args.probe], input="".join(rows), capture_output=True, text=True, timeout=20)
    assert result.returncode == 0, result.stderr
    actual = result.stdout.splitlines()
    assert len(actual) == len(expected)
    for index, (line, wanted) in enumerate(zip(actual, expected)):
        parts = line.split()
        got = (Decimal(parts[0]), Decimal(parts[1]), Decimal(parts[2]), int(parts[3]), Decimal(parts[4]), Decimal(parts[5]))
        assert got == wanted, f"synthetic arithmetic case {index} mismatch"
    print("exact decimal oracle: 1500 cases / 9000 comparisons passed")


if __name__ == "__main__":
    main()
