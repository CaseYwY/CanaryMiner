"""Compare ordered taxes through the real meter-bill CLI and private archive replay."""
import copy
from hashlib import sha256
import json
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

from test_meter_bill_named_cli import BUNDLE, HOUR, SCOPE, interval, named, plan, register, utc
from test_meter_demand_cli import declaration as demand_tariff
from test_meter_export_cli import export_tariff, independent_invoice, net_power, statement_period

UTC = {"kind": "utc", "fixed_offset_seconds": 0}


def unit(key, rate, included=False, basis="imported_kwh"):
    return {"key": key, "kind": basis, "rate": rate, "included": included}


def fraction(key, rate, components=(), prior=(), included=()):
    return {"key": key, "kind": "fraction", "rate": rate, "components": list(components),
            "prior_taxes": list(prior), "included_in": list(included)}


def period(begin, end, *rules, published=None):
    return {"begin_utc_ms": begin, "end_utc_ms": end, "published_utc_ms": begin if published is None else published,
            "rules": list(rules)}


def tax_tariff(value, *periods):
    value["schema_version"] = 15
    value["plan"]["tax_periods"] = list(periods)
    value["opening"]["tax_bases"] = {name: "0" for name in ("energy", "daily_fee", "cycle_fee", "demand", "billed_kwh")}
    return value


def main(cli, bundle=None):
    with TemporaryDirectory(prefix="canary-ordered-tax-") as directory:
        root = Path(directory)
        if bundle:
            installed = root / "installed \u03a9"
            installed.mkdir()
            cli = Path(shutil.copy2(cli, installed / cli.name))
            shutil.copytree(bundle, installed / BUNDLE)
        tariff, meter, invoice = [root / f"private {name}.json" for name in ("tariff", "meter", "invoice")]
        checked = 0

        def write(path, data):
            path.write_bytes(json.dumps(data, separators=(",", ":")).encode("ascii"))

        def run(*args, error=None):
            nonlocal checked
            result = subprocess.run([cli, "meter-bill", *map(str, args)], cwd=root, capture_output=True, text=True, timeout=20)
            assert all(name not in result.stdout + result.stderr for name in (tariff.name, meter.name, invoice.name, SCOPE))
            if error:
                assert result.returncode == 3 and not result.stdout and error in result.stderr, result
            else:
                assert result.returncode == 0 and result.stdout and not result.stderr, result
            checked += 1
            return result.stdout

        def review(policy, readings, cutoff, *options, error=None):
            write(tariff, policy)
            write(meter, readings)
            return run("--tariff", tariff, "--intervals", meter, "--as-of-utc-ms", cutoff, *options, error=error)

        if bundle:
            begin, end = utc("2024-11-03T04:00:00"), utc("2024-11-04T05:00:00")
            value = tax_tariff(plan(begin, begin, end, named("America/New_York")), period(
                begin, end, unit("excise", "0.2"), fraction("vat", "0.1", ("energy", "daily_fee"), ("excise",))))
            value["plan"]["revisions"][0]["charge_per_civil_day"] = "2"
            total = review(value, interval(begin, end), end, "--save-review", "named-tax.cmr")
            assert "Imported usage 25.000000 kWh" in total
            assert "Tax excise added 5.000000 USD on 25.000000 kWh" in total
            assert "Tax vat added 3.200000 USD on 32.000000 USD" in total
            assert "Cycle estimate at closing 35.200000 USD" in total
            assert run("--open-review", "named-tax.cmr") == total
            through = begin + HOUR
            value["opening"].update(through_utc_ms=through, imported_kwh="1", accrued_subtotal="3")
            value["opening"]["tax_bases"].update(energy="1", daily_fee="2")
            total = review(value, interval(through, end), end)
            assert "Change from imported readings 31.680000 USD" in total
            assert "Cycle estimate at closing 35.200000 USD" in total
        else:
            begin = utc("2024-01-01")
            middle, end = begin + HOUR, begin + 2 * HOUR
            policy = tax_tariff(plan(begin, begin, end, UTC), period(
                begin, end, unit("excise", "1"), fraction("vat", "0.1", ("energy",), ("excise",))))
            policy["plan"]["revisions"][0]["base_rate_per_kwh"] = "5"
            register_trace = register([(begin, "0"), (middle, "1"), (end, "2")])
            exclusive = review(policy, register_trace, end, "--save-review", "tax.cmr")
            assert "Declared supply subtotal before added taxes 10.000000 USD" in exclusive
            assert "Tax excise added 2.000000 USD on 2.000000 kWh" in exclusive
            assert "Tax vat added 1.200000 USD on 12.000000 USD" in exclusive
            assert "Cycle estimate at closing 13.200000 USD" in exclusive
            original = (root / "tax.cmr").read_bytes()
            tariff.unlink()
            meter.unlink()
            assert run("--open-review", "tax.cmr") == exclusive
            changed = original[:-65].replace(b'"kind":"fraction","rate":"0.1"', b'"kind":"fraction","rate":"0.2"')
            assert changed != original[:-65]
            (root / "tax.cmr").write_bytes(changed + sha256(changed).hexdigest().encode("ascii") + b"\n")
            run("--open-review", "tax.cmr", error="differ from the recalculated amounts")

            alternate = copy.deepcopy(policy)
            alternate["plan"]["tax_periods"][0]["rules"][1]["prior_taxes"] = []
            total = review(alternate, interval(begin, end), end)
            assert "Tax vat added 1.000000 USD on 10.000000 USD" in total
            assert "Cycle estimate at closing 13.000000 USD" in total

            inclusive = copy.deepcopy(policy)
            inclusive["plan"]["revisions"][0]["base_rate_per_kwh"] = "6.6"
            inclusive["plan"]["tax_periods"][0]["rules"] = [
                unit("excise", "1", included=True), fraction("vat", "0.1", included=("energy",))]
            total = review(inclusive, interval(begin, end), end)
            assert "Cycle estimate at closing 13.200000 USD" in total
            assert total.count("added 0.000000 USD | included in energy") == 2
            inclusive["plan"]["revisions"][0]["charge_per_civil_day"] = "4"
            inclusive["plan"]["tax_periods"][0]["rules"][1]["components"] = ["daily_fee"]
            total = review(inclusive, interval(begin, end), end)
            assert "Tax vat added 0.400000 USD on 4.000000 USD | included in energy" in total
            assert "Cycle estimate at closing 17.600000 USD" in total

            base = tax_tariff(demand_tariff(begin, end), period(begin, end,
                fraction("fee", "0.1", ("daily_fee", "cycle_fee", "demand", "minimum_adjustment"))))
            base["plan"].update(cycle_charge="1", minimum_cycle_subtotal="12")
            base["plan"]["revisions"][0]["charge_per_civil_day"] = "2"
            total = review(base, interval(begin, end), end)
            assert "Peak demand 1.000000 kW" in total
            assert "Minimum-charge adjustment 2.000000 USD" in total
            assert "Tax fee added 1.000000 USD on 10.000000 USD" in total
            assert "Cycle estimate at closing 13.000000 USD" in total
            prefix = review(base, interval(begin, middle), middle, "--save-review", "demand-prefix.cmr")
            assert "Tax fee added 0.800000 USD on 8.000000 USD" in prefix
            write(meter, {"schema_version": 1, "scope": SCOPE, "intervals": [
                interval(begin, middle)["intervals"][0], interval(middle, end)["intervals"][0]]})
            extended = run("--extend-review", "demand-prefix.cmr", "--intervals", meter, "--as-of-utc-ms",
                           end, "--save-review", "demand-complete.cmr")
            assert "Cycle estimate at closing 13.000000 USD" in extended
            assert run("--open-review", "demand-complete.cmr") == extended

            prefix = review(policy, register([(begin, "0"), (middle, "1")]), middle, "--save-review", "partial-tax.cmr")
            assert "Provisional cycle estimate 6.600000 USD" in prefix
            write(meter, register_trace)
            extended = run("--extend-review", "partial-tax.cmr", "--intervals", meter, "--as-of-utc-ms",
                           end, "--save-review", "extended-tax.cmr")
            assert "Cycle estimate at closing 13.200000 USD" in extended
            assert run("--open-review", "extended-tax.cmr") == extended
            midcycle = copy.deepcopy(policy)
            midcycle["opening"].update(through_utc_ms=middle, imported_kwh="1", accrued_subtotal="5")
            midcycle["opening"]["tax_bases"]["energy"] = "5"
            total = review(midcycle, register([(middle, "1"), (end, "2")]), end)
            assert "Change from imported readings 6.600000 USD" in total
            assert "Cycle estimate at closing 13.200000 USD" in total
            midcycle["opening"]["tax_bases"]["energy"] = "4"
            review(midcycle, register([(middle, "1"), (end, "2")]), end, error="saved tariff declaration")
            midcycle["opening"]["tax_bases"].update(energy="0", daily_fee="5")
            review(midcycle, register([(middle, "1"), (end, "2")]), end, error="saved tariff declaration")
            midcycle["plan"]["revisions"][0]["charge_per_civil_day"] = "4"
            midcycle["opening"]["accrued_subtotal"] = "9"
            midcycle["opening"]["tax_bases"].update(energy="5", daily_fee="4")
            total = review(midcycle, register([(middle, "1"), (end, "2")]), end)
            assert "Cycle estimate at closing 17.200000 USD" in total
            assert "Change from imported readings 6.600000 USD" in total

            rounded = tax_tariff(plan(begin, begin, middle, UTC), period(begin, middle,
                unit("physical", "1"), unit("rated", "2", basis="billed_kwh")))
            rounded["plan"]["rounding"] = "meter_import_half_hour_kwh_half_even_2"
            half = begin + HOUR // 2
            total = review(rounded, register([(begin, "0"), (half, "0.006"), (middle, "0.012")]), middle)
            assert "Tax physical added 0.012000 USD on 0.012000 kWh" in total
            assert "Tax rated added 0.040000 USD on 0.020000 kWh" in total
            assert "Cycle estimate at closing 0.072000 USD" in total
            rounded["opening"].update(through_utc_ms=half, imported_kwh="0.006", accrued_subtotal="0.01")
            rounded["opening"]["tax_bases"].update(energy="0.01", billed_kwh="0.01")
            total = review(rounded, register([(half, "0.006"), (middle, "0.012")]), middle)
            assert "Change from imported readings 0.036000 USD" in total
            for invalid_quantity in ("0.005", "0", "100"):
                rounded["opening"]["tax_bases"]["billed_kwh"] = invalid_quantity
                review(rounded, register([(half, "0.006"), (middle, "0.012")]), middle, error="saved tariff declaration")
            rounded["opening"]["imported_kwh"] = "0.004"
            rounded["opening"]["tax_bases"]["billed_kwh"] = "0"
            rounded_end = register([(half, "0.004"), (middle, "0.01")])
            for impossible_energy in ("0.01", "-0.01"):
                rounded["opening"]["accrued_subtotal"] = impossible_energy
                rounded["opening"]["tax_bases"]["energy"] = impossible_energy
                review(rounded, rounded_end, middle, error="saved tariff declaration")
            rounded["opening"]["accrued_subtotal"] = "0"
            rounded["opening"]["tax_bases"]["energy"] = "0"
            assert "Change from imported readings 0.036000 USD" in review(rounded, rounded_end, middle)

            # Two elapsed half-hours. Positive slots cannot round below zero and exact ties round to even.
            split = tax_tariff(plan(begin, begin, end, UTC), period(begin, end, unit("rated", "1", basis="billed_kwh")))
            split["plan"]["rounding"] = "meter_import_half_hour_kwh_half_even_2"
            for measured, billed, possible in (
                ("0.01", "0.02", False), ("0.001", "0.01", False), ("0.02", "0.01", False),
                ("0.03", "0.04", True), ("0.03", "0.02", True), ("0.015", "0.02", True), ("0.01", "0", True),
            ):
                opening = split["opening"]
                opening.update(through_utc_ms=middle, imported_kwh=measured, accrued_subtotal=billed)
                opening["tax_bases"].update(energy=billed, billed_kwh=billed)
                remaining = register([(middle, measured), (middle + HOUR // 2, measured), (end, measured)])
                if possible:
                    assert "Change from imported readings 0.000000 USD" in review(split, remaining, end)
                else:
                    review(split, remaining, end, error="saved tariff declaration")

            signed = tax_tariff(plan(begin, begin, end, UTC), period(begin, end,
                unit("rated", "0.5", basis="billed_kwh"), fraction("vat", "0.1", ("energy",))))
            signed["plan"]["revisions"][0]["base_rate_per_kwh"] = "-1"
            total = review(signed, interval(begin, end), end)
            assert "Tax rated added 1.000000 USD on 2.000000 kWh" in total
            assert "Tax vat added -0.200000 USD on -2.000000 USD" in total
            assert "Cycle estimate at closing -1.200000 USD" in total

            exported = tax_tariff(export_tariff(begin, end), period(begin, end, fraction("vat", "0.1", ("energy",))))
            statement = independent_invoice([statement_period(begin, end, supply_charge="3.30")],
                                            [statement_period(middle, end, export_payment="1.00")])
            write(invoice, statement)
            total = review(exported, net_power((begin, middle, "1000"), (middle, end, "-1000")), end, "--invoice", invoice)
            assert "Net electricity cost at closing 2.300000 USD" in total
            assert "Calculated supply charge at invoice precision 3.30 USD" in total
            assert "Calculated export payment at invoice precision 1.00 USD" in total
            assert total.count("(match)") == 2

            feb, mar = utc("2024-02-01"), utc("2024-03-01")
            recurring = tax_tariff(plan(begin, begin, feb, UTC, {"monthly_day": 1, "clock": UTC}, mar),
                period(begin, feb, fraction("vat", "0.1", ("energy",))),
                period(feb, mar, fraction("vat", "0.2", ("energy",)), published=begin))
            total = review(recurring, interval(begin, mar), mar)
            assert "Cycle estimate at closing 818.400000 USD" in total
            assert "Cycle estimate at closing 835.200000 USD" in total
            recurring["plan"]["tax_periods"][1]["end_utc_ms"] -= 1
            review(recurring, interval(begin, mar), mar, error="one declared tax period must cover the full supply cycle")
            late = copy.deepcopy(policy)
            late["confirmed_utc"] = (middle + 1000) // 1000
            late["plan"]["tax_periods"][0]["published_utc_ms"] = middle + 1000
            review(late, interval(begin, middle), middle, error="rate published after decision instant")
            assert "Cycle estimate at closing 13.200000 USD" in review(late, interval(begin, end), end)

            for rules in (
                [fraction("vat", "0.1", ("energy",), included=("energy",))],
                [fraction("vat", "0.1", ("energy",), ("future",)), unit("future", "1")],
                [unit("same", "1"), unit("same", "2")],
                [unit("included", "1", True), fraction("vat", "0.1", ("energy",), ("included",))],
                [unit("excise", "1"), fraction("vat", "0.1", ("energy",), ("excise", "excise"))],
                [fraction("vat", "0.1", ("energy", "energy"))],
            ):
                invalid = copy.deepcopy(policy)
                invalid["plan"]["tax_periods"][0]["rules"] = rules
                review(invalid, interval(begin, end), end, "--save-review", "invalid-tax.cmr",
                       error="saved tariff declaration")
                assert not (root / "invalid-tax.cmr").exists()
            invalid = copy.deepcopy(policy)
            invalid["plan"]["subtotal_multiplier"] = "1.1"
            review(invalid, interval(begin, end), end, error="saved tariff declaration")
        print(f"Ordered taxes: {checked} meter-bill CLI cases passed")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None)
