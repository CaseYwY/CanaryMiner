"""Exercise declared prepaid purchases through the native meter review and archive interface."""
from copy import deepcopy
import json
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

from test_meter_bill_named_cli import BUNDLE, SCOPE, interval, named, plan, register, utc

UTC = {"kind": "utc", "fixed_offset_seconds": 0}


def prepaid(opened, *, purchased="0", inventory="5", unpaid="0", purchases=()):
    return {
        "schema_version": 1, "scope": SCOPE, "currency": "USD", "published_utc_ms": opened,
        "purchase_reset": {"monthly_day": 1, "clock": UTC}, "fee_clock": UTC,
        "fee_per_civil_day": "1",
        "bands": [{"through_kwh": "100", "rate_per_kwh": "1"}, {"through_kwh": None, "rate_per_kwh": "2"}],
        "opening": {
            "through_utc_ms": opened, "purchased_in_cycle_kwh": purchased,
            "token_inventory_kwh": inventory, "unpaid_fee": unpaid,
        },
        "purchases": [{"utc_ms": at, "kwh": quantity, "cash_paid": cash} for at, quantity, cash in purchases],
    }


def main(cli, bundle=None):
    with TemporaryDirectory(prefix="canary-prepaid-review-") as tmp:
        root = Path(tmp)
        if bundle:
            installed = root / "installed \u03a9"
            installed.mkdir()
            cli = Path(shutil.copy2(cli, installed / cli.name))
            shutil.copytree(bundle, installed / BUNDLE)
        tariff, meter, tokens, invoice = [root / name for name in
                                          ("private tariff.json", "private meter.json",
                                           "private prepaid \u03a9.json", "private invoice.json")]

        def write(path, value):
            path.write_text(json.dumps(value, separators=(",", ":")), encoding="ascii")

        verified = 0

        def run(*args, error=None):
            nonlocal verified
            before = {p: p.read_bytes() for p in (tariff, meter, tokens, invoice) if p.exists()}
            proc = subprocess.run([cli, "meter-bill", *map(str, args)], cwd=root,
                                  capture_output=True, text=True, timeout=15)
            assert all(p.read_bytes() == data for p, data in before.items()), proc
            for secret in (tariff.name, meter.name, tokens.name, invoice.name, "private prepaid", SCOPE):
                assert secret not in proc.stdout + proc.stderr, proc
            if error:
                assert proc.returncode == 3 and not proc.stdout and error in proc.stderr, proc
            else:
                assert proc.returncode == 0 and proc.stdout and not proc.stderr, proc
            verified += 1
            return proc.stdout

        def review(policy, readings, record, cutoff, *flags, error=None):
            write(tariff, policy)
            write(meter, readings)
            write(tokens, record)
            return run("--tariff", tariff, "--intervals", meter, "--prepaid", tokens,
                       "--as-of-utc-ms", cutoff, *flags, error=error)

        def amount(report, label, value):
            assert f"{label} {value}" in report, report

        if bundle:
            begin = utc("2024-03-09T05:00:00")
            purchase = utc("2024-03-11T04:00:00")
            end = utc("2024-03-12T04:00:00")
            # The fee clock crosses two midnight boundaries in 47 hours. Purchase resets in Tokyo.
            rule = prepaid(begin, purchased="10", purchases=[(purchase, "2", "4")])
            rule["bands"][0]["through_kwh"] = "10"
            rule["purchase_reset"] = {"monthly_day": 11, "clock": named("Asia/Tokyo")}
            rule["fee_clock"] = named("America/New_York")
            policy = plan(begin, begin, end, UTC)
            policy["schema_version"] = 1
            readings = register([(begin, "100"), (purchase, "102"), (end, "105")])
            report = review(policy, readings, rule, end, "--save-review", "named.cmr")
            amount(report, "Modeled energy acquisition total", "2.000000 USD")
            amount(report, "Modeled daily fee collected total", "2.000000 USD")
            amount(report, "Unpaid daily fee", "1.000000 USD")
            amount(report, "Token inventory remaining", "2.000000 kWh")
            assert f"Purchase period [{utc('2024-03-10T15:00:00')}," in report
            assert run("--open-review", "named.cmr") == report
            bad = deepcopy(rule)
            bad["fee_clock"]["zone"] = "Unavailable/Clock"
            review(policy, readings, bad, end, error="invalid prepaid reset")
            bad = deepcopy(rule)
            bad["purchase_reset"]["clock"]["time_database_version"] = "1900z"
            review(policy, readings, bad, end, error="invalid prepaid reset")

            # Skipping a reset date is refused for sparse data as well as data with an intermediate reading.
            start, middle, finish = map(utc, ["2011-11-01", "2011-12-01", "2012-02-04"])
            apia = prepaid(start, inventory="0")
            apia["purchase_reset"] = {"monthly_day": 30, "clock": named("Pacific/Apia")}
            apia["fee_per_civil_day"] = "0"
            fixed = plan(start, start, finish, UTC)
            fixed["schema_version"] = 1
            for points in ([(start, "0"), (finish, "0")], [(start, "0"), (middle, "0"), (finish, "0")]):
                review(fixed, register(points), apia, finish, error="purchase reset clock is unavailable")
        else:
            begin, jan20, jan25, jan30, feb1, feb15, feb20, end, close = map(utc, [
                "2024-01-15", "2024-01-20", "2024-01-25", "2024-01-30", "2024-02-01",
                "2024-02-15", "2024-02-20", "2024-02-21", "2024-03-15",
            ])
            rules = prepaid(begin, purchases=[
                (jan25, "60", "70"), (jan30, "60", "85"), (feb1, "12", "14"), (feb20, "100", "130.95"),
            ])
            policy = plan(begin, begin, feb15, UTC, {"monthly_day": 15, "clock": UTC}, close)
            policy["schema_version"] = 2
            policy["plan"]["tiers"] = "progressive"
            policy["plan"]["revisions"][0].update(base_rate_per_kwh="0", bands=[
                {"through_kwh": "10", "rate_per_kwh": "1"}, {"through_kwh": None, "rate_per_kwh": "2"},
            ])
            points = [(begin, "100"), (jan20, "101"), (jan25, "102"), (jan30, "122"),
                      (feb1, "132"), (feb15, "132"), (feb20, "137"), (end, "140")]
            prefix_rules = deepcopy(rules)
            prefix_rules["purchases"] = prefix_rules["purchases"][:2]
            first = review(policy, register(points[:4]), prefix_rules, jan30, "--save-review", "prefix.cmr")
            amount(first, "Modeled energy acquisition total", "140.000000 USD")
            amount(first, "Modeled daily fee collected total", "15.000000 USD")
            amount(first, "Token inventory remaining", "103.000000 kWh")
            assert first.count("Daily fee collected in model 10.000000 USD") == 1
            assert first.count("Daily fee collected in model 5.000000 USD") == 1
            old = (root / "prefix.cmr").read_bytes()
            assert old.startswith(b"CANARY-METER-REVIEW-5\n")
            tokens.unlink()
            meter.unlink()
            assert run("--open-review", "prefix.cmr") == first

            merged = prepaid(begin, inventory="30", purchases=[(jan30, "120", "155")])
            combined = review(policy, register([(begin, "100"), (jan30, "122")]), merged, jan30)
            amount(combined, "Modeled energy acquisition total", "140.000000 USD")
            amount(combined, "Modeled daily fee collected total", "15.000000 USD")

            original = review(policy, register(points), rules, end)
            amount(original, "Cycle estimate at closing", "54.000000 USD")
            amount(original, "Provisional cycle estimate", "8.000000 USD")
            amount(original, "Modeled energy acquisition total", "264.000000 USD")
            amount(original, "Modeled daily fee collected total", "36.000000 USD")
            amount(original, "Declared cash paid total", "299.950000 USD")
            amount(original, "Cash paid minus modeled purchase total", "-0.050000 USD")
            amount(original, "Token inventory remaining", "197.000000 kWh")
            amount(original, "Unpaid daily fee", "1.000000 USD")
            amount(original, "Purchased in current purchase period", "112.000000 kWh")
            assert original.count("Daily fee collected in model 19.000000 USD") == 1
            assert original.count("Modeled energy acquisition charge 12.000000 USD") == 1

            # A different JSON field order does not change the purchase rules or saved prefix.
            write(tokens, dict(reversed(list(rules.items()))))
            flags = ("--extend-review", "prefix.cmr", "--intervals", meter, "--as-of-utc-ms", end)
            run(*flags, "--save-review", "no-sidecar.cmr", error="prepaid continuation")
            extended = run(*flags, "--prepaid", tokens, "--save-review", "later.cmr")
            assert "Extends verified meter coverage" in extended
            assert run("--open-review", "later.cmr") == extended
            assert (root / "prefix.cmr").read_bytes() == old

            changed = deepcopy(rules)
            changed["purchases"][0]["cash_paid"] = "71"
            changed["purchases"][1]["cash_paid"] = "84"
            write(tokens, changed)
            run(*flags, "--prepaid", tokens, "--save-review", "different.cmr", error="differ from the saved review prefix")
            assert not (root / "different.cmr").exists()
            changed = deepcopy(rules)
            changed["bands"][0]["through_kwh"] = "100.01"
            write(tokens, changed)
            run(*flags, "--prepaid", tokens, "--save-review", "repriced.cmr", error="differ from the saved review prefix")
            changed = deepcopy(rules)
            changed["published_utc_ms"] = jan30 + 1
            write(tokens, changed)
            run(*flags, "--prepaid", tokens, "--save-review", "late-terms.cmr",
                error="prepaid rule is published after the review cutoff")
            assert not (root / "late-terms.cmr").exists()

            # Optional invoice still compares gross supply. Prepaid amounts are available even for an open cycle.
            month = deepcopy(rules)
            month["purchases"] = month["purchases"][:3]
            write(invoice, {"schema_version": 1, "scope": SCOPE, "currency": "USD",
                            "decimal_places": 2, "rounding": "half_even", "cycles": [
                                {"begin_utc_ms": begin, "end_utc_ms": feb15, "supply_charge": "54.00"},
                            ]})
            report = review(policy, register(points[:6]), month, feb15, "--invoice", invoice,
                            "--save-review", "invoice.cmr")
            assert "Difference (calculated minus invoice) 0.00 USD (match)" in report
            assert "Modeled energy acquisition total 152.000000 USD" in report
            assert run("--open-review", "invoice.cmr") == report
            review(policy, register(points[:6]), month, feb15, "--save-review", "prepaid-uncompared.cmr")
            original = (root / "prepaid-uncompared.cmr").read_bytes()
            assert run("--open-review", "prepaid-uncompared.cmr", "--invoice", invoice,
                       "--save-review", "prepaid-compared.cmr") == report
            assert (root / "prepaid-uncompared.cmr").read_bytes() == original
            assert (root / "prepaid-compared.cmr").read_bytes() == (root / "invoice.cmr").read_bytes()
            assert run("--open-review", "prepaid-compared.cmr") == report

            small = prepaid(begin, inventory="0", purchases=[(jan20, "100", "100")])
            review(policy, register(points[:2]), small, jan20, "--save-review", "overdrawn.cmr",
                   error="inventory does not cover")
            assert not (root / "overdrawn.cmr").exists()
            no_boundary = prepaid(begin, purchases=[(jan25 - 1, "4", "4")])
            review(policy, register(points[:3]), no_boundary, jan25, error="reading at each prepaid purchase")
            review(policy, interval(begin, jan20, "0"), prepaid(begin), jan20,
                   error="requires cumulative meter register readings")
            review(policy, register(points[:2]), small, jan20 - 1, error="reading is after the review cutoff")
            review(policy, register(points[:2]), no_boundary, jan20, error="within the observed meter coverage")
            invalid = prepaid(begin)
            invalid["opening"]["through_utc_ms"] = begin + 1
            review(policy, register(points[:2]), invalid, jan20, error="through-time differs")
            invalid = prepaid(begin)
            invalid["scope"] = "different"
            review(policy, register(points[:2]), invalid, jan20, error="scope or currency differs")
            invalid = prepaid(begin)
            invalid["published_utc_ms"] = jan20 + 1
            review(policy, register(points[:2]), invalid, jan20, error="published after the review cutoff")
            invalid = prepaid(begin)
            invalid["bands"][-1]["through_kwh"] = "500"
            review(policy, register(points[:2]), invalid, jan20, error="unbounded final band")
            invalid = prepaid(begin, purchases=[(jan20, "1", "1"), (jan20, "2", "2")])
            review(policy, register(points[:2]), invalid, jan20, error="strictly increase")

            # A purchase-month reset leaves the annual quantity and the demand ratchet intact.
            year_begin, annual_start, annual_end, year_end = map(utc, [
                "2023-04-01", "2024-01-31", "2024-02-02", "2024-04-01",
            ])
            annual = plan(year_begin, annual_start, year_end, UTC,
                          {"annual_month": 4, "annual_day": 1, "clock": UTC})
            annual["schema_version"] = 5
            annual["opening"].update(imported_kwh="90", accrued_subtotal="90")
            annual["plan"]["tiers"] = "progressive"
            annual["plan"]["revisions"][0].update(base_rate_per_kwh="0", bands=[
                {"through_kwh": "100", "rate_per_kwh": "1"}, {"through_kwh": None, "rate_per_kwh": "2"},
            ])
            buys = prepaid(annual_start, inventory="20", purchased="99", purchases=[(feb1, "5", "6")])
            report = review(annual, register([(annual_start, "100"), (feb1, "100"), (annual_end, "115")]),
                            buys, annual_end)
            amount(report, "Provisional cycle estimate", "110.000000 USD")
            amount(report, "Modeled energy acquisition total", "5.000000 USD")

            demand = deepcopy(policy)
            demand["schema_version"] = 10
            demand["opening"].update(peak_demand_kw="0", demand_history=[{"cycle_end_utc_ms": begin, "peak_kw": "20"}])
            demand["demand_charge"] = {"window_minutes": 15, "offset_minutes": 0, "published_utc_ms": begin,
                                       "rate_per_kw": "5", "ratchet": {"lookback_months": 1, "fraction": "0.8"}}
            demand["plan"]["tiers"] = "none"
            demand["plan"]["revisions"][0]["bands"] = []
            quarter = 15 * 60 * 1000
            demand_meter = register([(begin, "0"), (feb1 - quarter, "0"), (feb1, "1"), (feb1 + quarter, "3.5"),
                                     (feb15, "3.5"), (feb20, "3.5")])
            buys = prepaid(begin, purchased="99", purchases=[(feb1, "2", "19")])
            report = review(demand, demand_meter, buys, feb20)
            amount(report, "Peak demand", "10.000000 kW")
            amount(report, "Demand ratchet floor", "16.000000 kW")
            amount(report, "Cycle estimate at closing", "80.000000 USD")
            amount(report, "Demand ratchet floor", "8.000000 kW")
            amount(report, "Purchased in current purchase period", "2.000000 kWh")

            # Opening arrears carry across a purchase reset even with no intervening acquisition.
            carry = prepaid(begin, inventory="0", unpaid="2", purchased="99",
                            purchases=[(feb1, "2", "21")])
            report = review(policy, register([(begin, "0"), (feb1, "0")]), carry, feb1)
            amount(report, "Modeled daily fee collected total", "19.000000 USD")
            amount(report, "Modeled energy acquisition total", "2.000000 USD")
            amount(report, "Token inventory remaining", "2.000000 kWh")

        print(f"Prepaid meter review passed {verified} cases")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None)
