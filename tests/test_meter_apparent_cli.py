"""Apparent maximum demand through the installed miner's ordinary meter review."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

from test_meter_demand_cli import BUNDLE, HOUR, MINUTE, SCOPE, UTC, declaration as active, modeled, named, utc


def tariff(begin, end, clock=UTC, minutes=15):
    policy = active(begin, end, clock, minutes=minutes)
    policy["schema_version"] = 11
    rule = policy["demand_charge"]
    rule["rate_per_kva"] = rule.pop("rate_per_kw")
    rule["power_factor_va_places"] = 6
    policy["opening"]["peak_demand_kva"] = policy["opening"].pop("peak_demand_kw")
    return policy


def samples(*intervals):
    source = modeled(*[(begin, end, watts) for begin, end, watts, _ in intervals])
    source["schema_version"] = 4
    for row, (_, _, _, evidence) in zip(source["intervals"], intervals):
        row.update(evidence)
    return source


def paired(*rows):
    return {"schema_version": 5, "scope": SCOPE, "register_readings": [
        {"utc_ms": when, "imported_kwh": active_kwh, "apparent_kvah": apparent_kvah}
        for when, active_kwh, apparent_kvah in rows
    ]}


def main(cli, bundle=None):
    with TemporaryDirectory(prefix="canary-apparent-meter-") as directory:
        root = Path(directory)
        if bundle:
            installed = root / "installed \u03a9"
            installed.mkdir()
            cli = Path(shutil.copy2(cli, installed / cli.name))
            shutil.copytree(bundle, installed / BUNDLE)
        policy_file, meter = root / "private tariff.json", root / "private meter.json"
        count = 0

        def write(path, doc):
            path.write_text(json.dumps(doc), encoding="ascii")

        def run(*args, error=None):
            nonlocal count
            result = subprocess.run([cli, "meter-bill", *map(str, args)], cwd=root,
                                    capture_output=True, text=True, timeout=20)
            for private in (SCOPE, policy_file.name, meter.name):
                assert private not in result.stdout + result.stderr, result
            if error:
                assert result.returncode == 3 and not result.stdout and error in result.stderr, result
            else:
                assert result.returncode == 0 and result.stdout and not result.stderr, result
            count += 1
            return result.stdout

        def review(policy, source, cutoff, archive=None, error=None):
            write(policy_file, policy)
            write(meter, source)
            options = ["--tariff", policy_file, "--intervals", meter, "--as-of-utc-ms", cutoff]
            if archive:
                options += ["--save-review", archive]
            return run(*options, error=error)

        def totals(report, peak, usage, amount):
            assert f"Peak demand {peak} kVA" in report, report
            assert f"Imported usage {usage} kWh" in report, report
            assert f"Cycle estimate at closing {amount} USD" in report, report

        if bundle:
            # Repeated local hours remain separate UTC windows.
            begin = utc("2024-11-03T04:00:00")
            end = begin + 4 * HOUR
            declaration = tariff(begin, end, named("America/New_York"), minutes=60)
            series = paired((begin, "0", "4"), (begin + HOUR, "1", "6"),
                            (begin + 2 * HOUR, "5", "11"), (begin + 3 * HOUR, "13", "21"), (end, "13", "22"))
            result = review(declaration, series, end, "named-kva.cmr")
            totals(result, "10.000000", "13.000000", "63.000000")
            assert "Reviewed IANA time database 2026e" in result
            assert run("--open-review", "named-kva.cmr") == result
        else:
            begin = utc("2024-01-01")
            quarter = begin + 15 * MINUTE
            end = quarter + 15 * MINUTE
            declaration = tariff(begin, quarter)
            converted = samples((begin, quarter, "8000", {"power_factor": "0.8"}))
            measured = samples((begin, quarter, "8000", {"average_va": "10000"}))
            original = review(declaration, converted, quarter, "converted.cmr")
            totals(original, "10.000000", "2.000000", "52.000000")
            assert "Demand charge before subtotal multiplier 50.000000 USD" in original
            policy_file.unlink()
            meter.unlink()
            assert run("--open-review", "converted.cmr") == original
            assert review(declaration, measured, quarter) == original
            totals(review(declaration, paired((begin, "10", "20"), (quarter, "12", "22.5")), quarter),
                   "10.000000", "2.000000", "52.000000")

            missing = "apparent demand needs whole-meter VA, an allowed power factor, or paired kWh/kVAh readings"
            review(declaration, modeled((begin, quarter, "0")), quarter, error=missing)
            review(declaration, samples((begin, quarter, "8000", {})), quarter, error=missing)
            review(declaration, samples((begin, quarter, "8000", {"average_va": "7999"})), quarter,
                   error="apparent VA must be at least")
            review(declaration, samples((begin, quarter, "8000", {"average_va": "10000", "power_factor": "0.8"})),
                   quarter, error="invalid meter interval")
            for bad_pf in ("0", "-0.8", "1.1"):
                review(declaration, samples((begin, quarter, "8000", {"power_factor": bad_pf})), quarter,
                       error="invalid power factor")
            old = active(begin, quarter)
            review(old, measured, quarter, error="require a saved kVA tariff")
            bad = copy.deepcopy(declaration)
            bad["opening"]["peak_demand_kw"] = "0"
            review(bad, measured, quarter, error="saved tariff declaration")

            measured_only = copy.deepcopy(declaration)
            measured_only["demand_charge"]["power_factor_va_places"] = None
            totals(review(measured_only, measured, quarter), "10.000000", "2.000000", "52.000000")
            review(measured_only, converted, quarter, error="requires measured apparent VA")

            # Proportional conversion is explicit and affects VA only.
            rounded = copy.deepcopy(declaration)
            rounded["demand_charge"]["power_factor_va_places"] = 0
            result = review(rounded, samples((begin, quarter, "1", {"power_factor": "0.4"})), quarter)
            totals(result, "0.002000", "0.000250", "0.010250")
            review(rounded, samples((begin, quarter, "1.2", {"power_factor": "1"})), quarter,
                   error="insufficient declared VA precision")

            five = begin + 5 * MINUTE
            weighted = samples((begin, five, "12000", {"power_factor": "0.8"}),
                               (five, quarter, "0", {"average_va": "6000"}))
            totals(review(declaration, weighted, quarter), "9.000000", "1.000000", "46.000000")
            totals(review(declaration, paired((begin, "0", "0"), (quarter, "0", "1")), quarter),
                   "4.000000", "0.000000", "20.000000")
            review(declaration, paired((begin, "0", "2"), (quarter, "0", "1")), quarter,
                   error="apparent register decreased")
            review(declaration, paired((begin, "0", "1"), (quarter, "2", "2")), quarter,
                   error="apparent register delta must be at least")
            two = tariff(begin, end)
            review(two, paired((begin, "0", "0"), (end, "0", "1")), end,
                   error="demand requires complete aligned windows")

            # An unchanged peak or matching VA from different evidence does not make a revised prefix acceptable.
            review(two, converted, quarter, "prefix.cmr")
            both = samples((begin, quarter, "8000", {"power_factor": "0.8"}),
                           (quarter, end, "8000", {"average_va": "12000"}))
            write(meter, both)
            result = run("--extend-review", "prefix.cmr", "--intervals", meter, "--save-review", "extended.cmr",
                         "--as-of-utc-ms", end)
            totals(result, "12.000000", "4.000000", "64.000000")
            assert run("--open-review", "extended.cmr") == result
            both["intervals"][0].pop("power_factor")
            both["intervals"][0]["average_va"] = "10000"
            write(meter, both)
            run("--extend-review", "prefix.cmr", "--intervals", meter, "--save-review", "bad.cmr",
                "--as-of-utc-ms", end, error="differ from the saved review prefix")

            original_pair = paired((begin, "7", "8"), (quarter, "9", "10.5"))
            review(two, original_pair, quarter, "pair.cmr")
            write(meter, paired((begin, "7", "9"), (quarter, "9", "11.5"), (end, "11", "14.5")))
            run("--extend-review", "pair.cmr", "--intervals", meter, "--save-review", "bad-pair.cmr",
                "--as-of-utc-ms", end, error="differ from the saved review prefix")

            mid = copy.deepcopy(two)
            mid["opening"].update(through_utc_ms=quarter, imported_kwh="0", peak_demand_kva="10")
            result = review(mid, samples((quarter, end, "0", {"average_va": "12000"})), end)
            totals(result, "12.000000", "0.000000", "60.000000")
            assert "Change from imported readings 10.000000 USD" in result

            feb, mar = utc("2024-02-01"), utc("2024-03-01")
            monthly = tariff(begin, feb)
            monthly["reset_policy"] = {"monthly_day": 1, "clock": UTC}
            monthly["plan"]["revisions"][0]["end_utc_ms"] = mar
            monthly["demand_charge"]["ratchet"] = {"lookback_months": 1, "fraction": "0.8"}
            monthly["opening"]["demand_history"] = [{"cycle_end_utc_ms": begin, "peak_kva": "20"}]
            series = samples((begin, feb, "0", {"average_va": "12000"}),
                             (feb, mar, "0", {"average_va": "8000"}))
            result = review(monthly, series, mar, "monthly.cmr")
            assert "Peak demand 12.000000 kVA" in result and "Peak demand 8.000000 kVA" in result
            assert "Billable demand 16.000000 kVA" in result and "Billable demand 9.600000 kVA" in result
            assert run("--open-review", "monthly.cmr") == result
            monthly["opening"]["demand_history"][0] = {"cycle_end_utc_ms": begin, "peak_kw": "20"}
            review(monthly, series, mar, error="saved tariff declaration")

        print(f"Apparent demand: {count} ordinary meter-bill invocations passed")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None)
