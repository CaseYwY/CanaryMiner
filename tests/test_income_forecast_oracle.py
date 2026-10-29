"""Exact end-to-end forecast/net arithmetic against independent Fraction, no runtime sources."""
from decimal import Decimal, localcontext, ROUND_HALF_EVEN
from fractions import Fraction
import random
import subprocess
import sys


def main():
    rng = random.Random(20261029)
    cases, expected = [], []

    def number(low=0, high=10**8, scale=6):
        return f'{Decimal(rng.randint(low, high)).scaleb(-scale):f}'

    for _ in range(1500):
        credits, rate = number(), number(1)
        cal_ms = rng.randint(1, 86400000)
        actual, future = number(-10**8), number()
        user_ms = rng.randint(0, 43200000)
        power, price, fx, tariff, currency_fx = number(), number(1), number(1), number(-10**8), number(1)
        fields = [credits, rate, str(cal_ms), actual, future, str(user_ms), power, price, fx, tariff, currency_fx]
        cases.append(' '.join(fields) + '\n')
        c, r, duration, a, f, eligible, watts, prl, stable, energy_rate, conversion = map(Fraction, fields)
        forecast = a + c * f * eligible / (r * duration)
        usd = forecast * prl * stable
        cost = watts * 86400000 * energy_rate * conversion / 3600000000
        expected.append((forecast, usd, usd - cost))
    result = subprocess.run([sys.argv[1]], input=''.join(cases), capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, 'synthetic forecast oracle exited unsuccessfully'
    rows = result.stdout.splitlines()
    assert len(rows) == len(expected)
    with localcontext() as context:
        context.prec = 220
        for index, (row, values) in enumerate(zip(rows, expected)):
            fields = row.split()
            assert len(fields) == 9
            for offset, expected_value in zip(range(0, 9, 3), values):
                assert Fraction(fields[offset]) / Fraction(fields[offset + 1]) == expected_value, f'exact forecast case {index}'
                displayed = (Decimal(expected_value.numerator) / Decimal(expected_value.denominator)).quantize(Decimal('0.00000001'), rounding=ROUND_HALF_EVEN)
                assert Decimal(fields[offset + 2]) == displayed, f'display forecast case {index}'
    print('income forecast oracle: 1500 cases / 9000 exact-and-display comparisons passed')


if __name__ == '__main__':
    main()
