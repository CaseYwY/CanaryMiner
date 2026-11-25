"""Demand-window and peak-charge coverage through the ordinary meter-bill entry point."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

from test_meter_bill_named_cli import BUNDLE, HOUR, SCOPE, named, plan, register, utc

MINUTE = 60000
UTC = {"kind": "utc", "fixed_offset_seconds": 0}


def declaration(begin, end, clock=UTC, reset="explicit_cycle_only", rates_end=None, minutes=15):
    value = plan(begin, begin, end, clock, reset, rates_end)
    value["schema_version"] = 9
    value["demand_charge"] = {
        "window_minutes": minutes, "offset_minutes": 0, "published_utc_ms": begin, "rate_per_kw": "5",
    }
    value["opening"]["peak_demand_kw"] = "0"
    return value


def modeled(*intervals):
    return {"schema_version": 1, "scope": SCOPE, "intervals": [
        {"begin_utc_ms": a, "end_utc_ms": b, "average_watts": power} for a, b, power in intervals
    ]}


def main(cli, bundle=None):
    with TemporaryDirectory(prefix="canary-demand-meter-") as directory:
        root = Path(directory)
        if bundle:
            installed = root / "installed \u03a9"
            installed.mkdir()
            cli = Path(shutil.copy2(cli, installed / cli.name))
            shutil.copytree(bundle, installed / BUNDLE)
        tariff, meter = root / "private tariff.json", root / "private meter.json"
        checks = 0

        def write(path, doc):
            path.write_text(json.dumps(doc), encoding="ascii")

        def run(*args, error=None):
            nonlocal checks
            result = subprocess.run([cli, "meter-bill", *map(str, args)], cwd=root,
                                    capture_output=True, text=True, timeout=20)
            assert all(x not in result.stdout + result.stderr for x in (SCOPE, tariff.name, meter.name))
            if error:
                assert result.returncode == 3 and not result.stdout and error in result.stderr, result
            else:
                assert result.returncode == 0 and result.stdout and not result.stderr, result
            checks += 1
            return result.stdout

        def review(policy, source, cutoff, archive=None, error=None):
            write(tariff, policy)
            write(meter, source)
            args = ["--tariff", tariff, "--intervals", meter, "--as-of-utc-ms", cutoff]
            if archive:
                args += ["--save-review", archive]
            return run(*args, error=error)

        def totals(text, peak, amount):
            assert f"Peak demand {peak} kW" in text, text
            assert f"Cycle estimate at closing {amount} USD" in text, text

        if bundle:
            # Repeated local 01:00 hours remain distinct UTC demand windows, with one cycle-wide peak.
            begin = utc("2024-11-03T04:00:00")
            end = begin + 4 * HOUR
            policy = declaration(begin, end, named("America/New_York"), minutes=60)
            readings = register([(begin, "0"), (begin + HOUR, "1"), (begin + 2 * HOUR, "5"),
                                 (begin + 3 * HOUR, "13"), (end, "13")])
            report = review(policy, readings, end, archive="named.cmr")
            totals(report, "8.000000", "53.000000")
            assert "Reviewed IANA time database 2026e" in report
            assert run("--open-review", "named.cmr") == report
        else:
            begin = utc("2024-01-01")
            cut, end = begin + 5 * MINUTE, begin + 15 * MINUTE
            policy = declaration(begin, end)
            power = modeled((begin, cut, "12000"), (cut, end, "0"))
            use = register([(begin, "0"), (cut, "1"), (end, "1")])
            expected = review(policy, power, end, archive="demand.cmr")
            totals(expected, "4.000000", "21.000000")
            assert "Imported usage 1.000000 kWh" in expected
            assert "Demand charge before subtotal multiplier 20.000000 USD" in expected
            tariff.unlink()
            meter.unlink()
            assert run("--open-review", "demand.cmr") == expected
            totals(review(policy, use, end), "4.000000", "21.000000")

            short = declaration(begin, end, minutes=5)
            totals(review(short, power, end), "12.000000", "61.000000")
            totals(review(short, use, end), "12.000000", "61.000000")
            boundary = "demand requires complete aligned windows"
            review(short, register([(begin, "0"), (end, "1")]), end, error=boundary)
            review(policy, modeled((begin, cut, "12000")), cut, error=boundary)

            shifted = declaration(begin + MINUTE, end + MINUTE)
            shifted["demand_charge"]["offset_minutes"] = 1
            totals(review(shifted, register([(begin + MINUTE, "0"), (end + MINUTE, "1")]), end + MINUTE),
                   "4.000000", "21.000000")
            shifted["demand_charge"]["offset_minutes"] = 0
            review(shifted, register([(begin + MINUTE, "0"), (end + MINUTE, "1")]), end + MINUTE,
                   error="saved tariff declaration")

            # Billing quantity rounding must not change the physical peak.
            halfway, rounded_end = begin + HOUR // 2, begin + HOUR
            rounded = declaration(begin, rounded_end, minutes=30)
            rounded["plan"]["rounding"] = "meter_import_half_hour_kwh_half_even_2"
            report = review(rounded, register([(begin, "0"), (halfway, "0.005"), (rounded_end, "0.02")]),
                            rounded_end)
            totals(report, "0.030000", "0.170000")
            assert "Billed usage 0.020000 kWh" in report

            # Review continuation retains the known peak until a later complete window establishes a new one.
            close = end + 15 * MINUTE
            later = declaration(begin, close)
            provisional = review(later, use, end, archive="prefix.cmr")
            assert "Peak demand 4.000000 kW" in provisional
            assert "Provisional cycle estimate 21.000000 USD" in provisional
            old_bytes = (root / "prefix.cmr").read_bytes()
            write(meter, register([(begin, "0"), (cut, "1"), (end, "1"), (close, "3")]))
            extended = run("--extend-review", "prefix.cmr", "--intervals", meter, "--as-of-utc-ms", close,
                           "--save-review", "extended.cmr")
            totals(extended, "8.000000", "43.000000")
            assert (root / "prefix.cmr").read_bytes() == old_bytes
            assert run("--open-review", "extended.cmr") == extended

            declared = copy.deepcopy(later)
            declared["opening"].update(through_utc_ms=end, imported_kwh="2.5",
                                       accrued_subtotal="1", peak_demand_kw="10")
            headroom = review(declared, modeled((end, close, "8000")), close)
            totals(headroom, "10.000000", "53.000000")
            assert "Change from imported readings 2.000000 USD" in headroom
            higher = review(declared, modeled((end, close, "12000")), close)
            totals(higher, "12.000000", "64.000000")
            assert "Change from imported readings 13.000000 USD" in higher
            for impossible in ("2.499", "2.501"):
                declared["opening"]["imported_kwh"] = impossible
                review(declared, modeled((end, close, "8000")), close, error="saved tariff declaration")

            # Minimum and subtotal multiplier are applied after the demand charge.
            floor = copy.deepcopy(policy)
            floor["plan"].update(cycle_charge="1", minimum_cycle_subtotal="25", subtotal_multiplier="1.1")
            totals(review(floor, power, end), "4.000000", "27.500000")
            late = copy.deepcopy(policy)
            late["confirmed_utc"] = (end + MINUTE) // 1000
            late["demand_charge"]["published_utc_ms"] = end + MINUTE
            review(late, power, end, error="rate published after decision instant")
            totals(review(late, power, end + MINUTE), "4.000000", "21.000000")

            feb, mar = map(utc, ("2024-02-01", "2024-03-01"))
            monthly = declaration(begin, feb, reset={"monthly_day": 1, "clock": UTC}, rates_end=mar)
            monthly["plan"]["revisions"][0]["base_rate_per_kwh"] = "0"
            bill = review(monthly, modeled((begin, feb, "4000"), (feb, mar, "1000")), mar)
            totals(bill, "4.000000", "20.000000")
            totals(bill, "1.000000", "5.000000")

            # A legacy declaration with no new rule keeps its old amount and report shape.
            legacy = copy.deepcopy(policy)
            legacy["schema_version"] = 1
            review(legacy, power, end, error="saved tariff declaration")
            legacy.pop("demand_charge")
            legacy["opening"].pop("peak_demand_kw")
            original = review(legacy, power, end)
            assert "Peak demand" not in original and "Cycle estimate at closing 1.000000 USD" in original
            for change in (
                lambda x: x["demand_charge"].update(unit="kVA"),
                lambda x: x["demand_charge"].update(rate_per_kw="-1"),
                lambda x: x["demand_charge"].update(window_minutes=7),
                lambda x: x["opening"].pop("peak_demand_kw"),
                lambda x: x["opening"].update(peak_demand_kw="1"),
            ):
                invalid = copy.deepcopy(policy)
                change(invalid)
                review(invalid, power, end, error="saved tariff declaration")
        print(f"Demand windows: {checks} ordinary meter-bill invocations passed")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None)
