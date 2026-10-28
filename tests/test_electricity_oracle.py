"""Deterministic, entirely synthetic independent Python Decimal oracle; no dependencies or network."""
import decimal
import random
import subprocess
import sys

D = decimal.Decimal
denominator = D(3600000000)
unit = D("0.000000000001")


def display(value):
    # C++ display suppresses signed zero.
    result = value.quantize(unit, rounding=decimal.ROUND_HALF_EVEN)
    return format(abs(result) if not result else result, ".12f")


def run(probe):
    rng = random.Random(20261028)
    lines, expected = [], []
    for i in range(500):
        watts = D(rng.randrange(0, 10**10)) / 1000000
        elapsed = rng.randrange(1, 86400001)
        numerator = watts * elapsed
        lines.append(f"sample {watts} {elapsed}")
        expected.append([numerator, display(numerator / denominator)])
    for i in range(1000):
        mode = ["none", "progressive", "all"][(i // 3) % 3]
        threshold = D(rng.randrange(1, 1000000)) / 1000
        quantity = threshold + D([-1, 0, 1][i % 3]) / 1000000 if i < 90 else D(rng.randrange(0, 10**12)) / 1000000
        r1, r2, base = [D(rng.randrange(-10**7, 10**7)) / 1000000 for _ in range(3)]
        factor = D(rng.randrange(0, 3001)) / 1000
        floor = None if i % 4 else D(rng.randrange(0, 10000)) / 100
        tier = D(0)
        if mode == "progressive":
            tier = min(quantity, threshold) * r1 + max(D(0), quantity - threshold) * r2
        elif mode == "all":
            tier = quantity * (r1 if quantity <= threshold else r2)
        cost = quantity * base + tier
        if floor is not None:
            cost = max(floor, cost)
        cost *= factor
        lines.append(f"bill {mode} {quantity} {r1} {r2} {threshold} {base} {factor} {floor if floor is not None else 'none'}")
        expected.append([cost * denominator, display(cost), quantity * denominator])

    result = subprocess.run([probe], input="\n".join(lines) + "\n", capture_output=True, text=True, timeout=45, check=True)
    outputs = result.stdout.splitlines()
    assert len(outputs) == len(expected), (len(outputs), len(expected))
    checks = 0
    for index, (actual, wanted) in enumerate(zip(outputs, expected)):
        fields = actual.split()
        assert len(fields) == len(wanted), index
        for field, reference in zip(fields, wanted):
            assert (D(field) == reference if isinstance(reference, D) else field == reference), (index, field, reference)
            checks += 1
    print(f"electricity oracle: {len(expected)} cases, {checks} exact/display comparisons passed")


if __name__ == "__main__":
    with decimal.localcontext() as context:
        context.prec = 220
        run(sys.argv[1])
