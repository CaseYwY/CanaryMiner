"""Rolling actual demand peaks through the native meter review and archive paths."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

from test_meter_demand_cli import BUNDLE, MINUTE, SCOPE, UTC, declaration as demand, modeled, named, utc


def declaration(begin, end, history, rates_end=None, reset_clock=UTC):
    value = demand(begin, end, reset={"monthly_day": 1, "clock": reset_clock}, rates_end=rates_end)
    value["schema_version"] = 10
    value["demand_charge"]["ratchet"] = {"lookback_months": len(history), "fraction": "0.8"}
    value["opening"]["demand_history"] = [
        {"cycle_end_utc_ms": boundary, "peak_kw": peak} for boundary, peak in history
    ]
    value["plan"]["revisions"][0]["base_rate_per_kwh"] = "0"
    return value


def main(cli, bundle=None):
    with TemporaryDirectory(prefix="canary-demand-ratchet-") as directory:
        root = Path(directory)
        if bundle:
            installed = root / "installed \u03a9"
            installed.mkdir()
            cli = Path(shutil.copy2(cli, installed / cli.name))
            shutil.copytree(bundle, installed / BUNDLE)
        tariff, meter = root / "private tariff.json", root / "private meter.json"
        checks = 0

        def write(path, value):
            path.write_text(json.dumps(value), encoding="ascii")

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
            options = ["--tariff", tariff, "--intervals", meter, "--as-of-utc-ms", cutoff]
            if archive:
                options += ["--save-review", archive]
            return run(*options, error=error)

        def amounts(report, physical, floor, billable, total):
            for line in (f"Peak demand {physical} kW", f"Demand ratchet floor {floor} kW",
                         f"Billable demand {billable} kW", f"Cycle estimate at closing {total} USD"):
                assert line in report, report

        if bundle:
            feb, march, april, may = map(utc, (
                "2024-02-01T05:00:00", "2024-03-01T05:00:00", "2024-04-01T04:00:00", "2024-05-01T04:00:00"))
            rule = declaration(march, april, [(feb, "4"), (march, "20")], may, named("America/New_York"))
            report = review(rule, modeled((march, april, "10000"), (april, may, "17000")),
                            may, archive="dst.cmr")
            assert "Reviewed IANA time database 2026e" in report
            assert "Demand ratchet floor 16.000000 kW" in report
            assert "Cycle estimate at closing 80.000000 USD" in report
            assert "Cycle estimate at closing 85.000000 USD" in report
            assert run("--open-review", "dst.cmr") == report
            wrong = copy.deepcopy(rule)
            wrong["opening"]["demand_history"][0]["cycle_end_utc_ms"] -= 3600000
            review(wrong, modeled((march, april, "10000")), april, error="saved tariff declaration")
        else:
            nov, dec, jan, feb, mar, apr, may = map(utc, (
                "2023-11-01", "2023-12-01", "2024-01-01", "2024-02-01",
                "2024-03-01", "2024-04-01", "2024-05-01"))
            rule = declaration(jan, feb, [(nov, "4"), (dec, "8"), (jan, "20")], may)
            prefix = review(rule, modeled((jan, feb, "10000")), feb, archive="first.cmr")
            amounts(prefix, "10.000000", "16.000000", "16.000000", "80.000000")
            first_bytes = (root / "first.cmr").read_bytes()
            trace = modeled((jan, feb, "10000"), (feb, mar, "15000"),
                            (mar, apr, "17000"), (apr, may, "1000"))
            write(meter, trace)
            report = run("--extend-review", "first.cmr", "--intervals", meter, "--as-of-utc-ms", may,
                         "--save-review", "all.cmr")
            added = report.split("\nCycle ")
            amounts(added[1], "10.000000", "16.000000", "16.000000", "80.000000")
            amounts(added[2], "15.000000", "16.000000", "16.000000", "80.000000")
            amounts(added[3], "17.000000", "16.000000", "17.000000", "85.000000")
            amounts(added[4], "1.000000", "13.600000", "13.600000", "68.000000")
            assert (root / "first.cmr").read_bytes() == first_bytes
            tariff.unlink()
            meter.unlink()
            assert run("--open-review", "all.cmr") == report

            already = copy.deepcopy(rule)
            middle = jan + 15 * MINUTE
            already["opening"].update(through_utc_ms=middle, imported_kwh="2.5",
                                      accrued_subtotal="1", peak_demand_kw="10")
            previous = review(already, modeled((middle, feb, "12000")), feb)
            amounts(previous, "12.000000", "16.000000", "16.000000", "81.000000")
            assert "Change from imported readings 0.000000 USD" in previous
            higher = review(already, modeled((middle, feb, "17000")), feb)
            amounts(higher, "17.000000", "16.000000", "17.000000", "86.000000")
            assert "Change from imported readings 5.000000 USD" in higher

            for history_peak, amount in (("20.000000624999999999", "80.000002"),
                                         ("20.000000625", "80.000002"),
                                         ("20.000000625000000001", "80.000003")):
                tie = declaration(jan, feb, [(jan, history_peak)])
                result = review(tie, modeled((jan, feb, "0")), feb)
                assert f"Cycle estimate at closing {amount} USD" in result, result

            # History rows share the same 512-row bound as rate revisions.
            bounded = declaration(jan, feb, [(jan, "20")])
            rates = bounded["plan"]["revisions"]
            cut = jan + 14 * 24 * 60 * MINUTE
            rates.append(copy.deepcopy(rates[0]))
            for index, (start, end, count) in enumerate(((jan, cut, 255), (cut, feb, 254))):
                edges = [start + (end - start) * i // count for i in range(count + 1)]
                rates[index].update(key=f"bounded-{index}", begin_utc_ms=start, end_utc_ms=end, time_mode="dynamic",
                                    dynamic=[{"begin_utc_ms": a, "end_utc_ms": b,
                                              "published_utc_ms": jan, "rate_per_kwh": "0"}
                                             for a, b in zip(edges, edges[1:])])
            result = review(bounded, modeled((jan, feb, "0")), feb)
            amounts(result, "0.000000", "16.000000", "16.000000", "80.000000")
            rows = rates[1]["dynamic"]
            first = dict(rows[0])
            rows[0]["begin_utc_ms"] += 1
            first["end_utc_ms"] = rows[0]["begin_utc_ms"]
            rows.insert(0, first)
            review(bounded, modeled((jan, feb, "0")), feb, error="saved tariff declaration")

            for mutation in (
                lambda v: v.update(schema_version=9),
                lambda v: v.update(reset_policy="explicit_cycle_only"),
                lambda v: v.update(reset_policy={"annual_month": 1, "annual_day": 1, "clock": UTC}),
                lambda v: v["opening"]["demand_history"].pop(),
                lambda v: v["opening"]["demand_history"].reverse(),
                lambda v: v["opening"]["demand_history"][0].update(peak_kw="-1"),
                lambda v: v["demand_charge"]["ratchet"].update(fraction="1.1"),
                lambda v: v["demand_charge"]["ratchet"].update(lookback_months=0),
            ):
                wrong = copy.deepcopy(rule)
                mutation(wrong)
                review(wrong, modeled((jan, feb, "10000")), feb, error="saved tariff declaration")
            old = copy.deepcopy(rule)
            old.update(schema_version=9)
            old["demand_charge"].pop("ratchet")
            old["opening"].pop("demand_history")
            original = review(old, modeled((jan, feb, "10000")), feb)
            assert "Demand ratchet floor" not in original and "Cycle estimate at closing 50.000000 USD" in original
        print(f"Demand ratchet: {checks} ordinary meter-bill invocations passed")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None)
