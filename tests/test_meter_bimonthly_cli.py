"""Verify explicit monthly-average pricing through ordinary meter-bill reviews."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

from test_meter_bill_named_cli import BUNDLE, SCOPE, interval, named, plan, register, utc

FIXED = {"kind": "utc", "fixed_offset_seconds": 0}


def declaration(begin, end, following, start_month, day, reset_clock=FIXED):
    policy = {"bimonthly_start_month": start_month, "bimonthly_day": day, "clock": reset_clock}
    draft = plan(begin, begin, end, FIXED, policy, following)
    draft["schema_version"] = 6
    draft["plan"].update(tiers="all_units", minimum_cycle_subtotal=None)
    draft["plan"]["revisions"][0].update(base_rate_per_kwh="0", bands=[
        {"through_kwh": "100", "rate_per_kwh": "1"},
        {"through_kwh": None, "rate_per_kwh": "2"},
    ])
    return draft


def main(binary, bundle=None):
    with TemporaryDirectory(prefix="canary-bimonthly-") as folder:
        work = Path(folder)
        if bundle:
            installed = work / "installed \u03a9"
            installed.mkdir()
            binary = Path(shutil.copy2(binary, installed / binary.name))
            shutil.copytree(bundle, installed / BUNDLE)
        rates, meter = work / "private schedule.json", work / "private meter.json"
        count = 0

        def save(path, doc):
            path.write_text(json.dumps(doc), encoding="ascii")

        def run(*args, code=0, error=None):
            nonlocal count
            outcome = subprocess.run([binary, "meter-bill", *map(str, args)], cwd=work,
                                     capture_output=True, text=True, timeout=20)
            assert outcome.returncode == code, outcome
            assert not any(label in outcome.stdout + outcome.stderr for label in (SCOPE, rates.name, meter.name))
            if code:
                assert not outcome.stdout and (not error or error in outcome.stderr), outcome
            else:
                assert outcome.stdout and not outcome.stderr, outcome
            count += 1
            return outcome.stdout

        def review(when, *args, **kwargs):
            return run("--tariff", rates, "--intervals", meter, "--as-of-utc-ms", when, *args, **kwargs)

        if bundle:
            # London's spring transition is after the March 31 reset, so the second cycle loses an hour.
            begin = utc("2024-01-31T00:00:00")
            end = utc("2024-03-31T00:00:00")
            next_end = utc("2024-05-30T23:00:00")
            config = declaration(begin, end, next_end, 1, 31, named("Europe/London"))
            save(rates, config)
            save(meter, register([(begin, "31"), (end, "233"), (next_end, "434")]))
            report = review(next_end, "--save-review", "named.cmr")
            assert f"[{begin}, {end})" in report and f"[{end}, {next_end})" in report
            assert "Cycle estimate at closing 404.000000 USD" in report
            assert "Cycle estimate at closing 402.000000 USD" in report
            assert run("--open-review", "named.cmr") == report
            config["opening"]["cycle_end_utc_ms"] += 3600000
            save(rates, config)
            review(next_end, code=3, error="invalid")
            (installed / BUNDLE / "version").write_text("changed", encoding="ascii")
            run("--open-review", "named.cmr", code=3, error="time database unavailable or invalid")
        else:
            begin, mid, end = map(utc, ("2024-01-01", "2024-02-01", "2024-03-01"))
            next_end = utc("2024-05-01")
            config = declaration(begin, end, next_end, 1, 1)
            save(rates, config)
            for usage, amount, average in (
                ("199.5", "199.500000", "99.750000"),
                ("200", "200.000000", "100.000000"),
                ("200.000002", "400.000004", "100.000001"),
                ("202", "404.000000", "101.000000"),
            ):
                save(meter, register([(begin, "0"), (end, usage)]))
                report = review(end)
                assert f"Cycle estimate at closing {amount} USD" in report, report
                assert f"Monthly-average usage at closing {average} kWh" in report, report

            save(meter, register([(begin, "1000"), (mid, "1120")]))
            report = review(mid, "--save-review", "prefix.cmr")
            assert "Provisional cycle estimate 120.000000 USD" in report
            assert "Closing charges depend on full-cycle usage" in report
            assert "Monthly-average usage at closing" not in report
            before = (work / "prefix.cmr").read_bytes()
            save(meter, register([(begin, "1000"), (mid, "1120"), (end, "1202"), (next_end, "1262")]))
            report = run("--extend-review", "prefix.cmr", "--intervals", meter, "--as-of-utc-ms", next_end,
                         "--save-review", "extended.cmr")
            assert "Cycle estimate at closing 404.000000 USD" in report
            assert "Cycle estimate at closing 60.000000 USD" in report
            assert (work / "prefix.cmr").read_bytes() == before
            assert run("--open-review", "extended.cmr") == report

            save(meter, register([(begin, "1000"), (mid, "1120"), (next_end, "1262")]))
            review(next_end, code=3, error="needs a reading at the billing-cycle boundary")

            config["plan"].update(cycle_charge="15", minimum_cycle_subtotal="125", subtotal_multiplier="1.2")
            save(meter, register([(begin, "0"), (end, "50")]))
            save(rates, config)
            assert "Cycle estimate at closing 300.000000 USD" in review(end)
            save(meter, register([(begin, "0"), (end, "202")]))
            assert "Cycle estimate at closing 520.800000 USD" in review(end)
            config["plan"].update(cycle_charge="0", minimum_cycle_subtotal=None, subtotal_multiplier="1")
            config["plan"]["revisions"][0]["bands"][1]["rate_per_kwh"] = "-2"
            save(rates, config)
            assert "Cycle estimate at closing -404.000000 USD" in review(end)

            config["plan"]["revisions"][0]["bands"][1]["rate_per_kwh"] = "2"
            save(rates, config)
            save(meter, interval(begin, next_end, "100"))
            report = review(next_end)
            assert "Cycle estimate at closing 144.000000 USD" in report
            assert "Cycle estimate at closing 146.400000 USD" in report

            # December's even reset precedes January, and day 31 clamps to leap day in February.
            december, february, april = map(utc, ("2023-12-31", "2024-02-29", "2024-04-30"))
            even = declaration(december, february, april, 2, 31)
            save(rates, even)
            save(meter, register([(december, "0"), (february, "202"), (april, "400")]))
            report = review(april)
            assert f"[{december}, {february})" in report and f"[{february}, {april})" in report
            assert "Cycle estimate at closing 198.000000 USD" in report
            even["opening"].update(through_utc_ms=utc("2024-01-31"), imported_kwh="120", accrued_subtotal="120")
            save(rates, even)
            save(meter, register([(utc("2024-01-31"), "120"), (february, "202")]))
            report = review(february)
            assert "Cycle estimate at closing 404.000000 USD (includes declared opening)" in report
            assert "Change from imported readings 284.000000 USD" in report
            assert "Monthly-average usage at closing 101.000000 kWh" in report

            for update in (
                lambda doc: doc.update(schema_version=5),
                lambda doc: doc.update(reset_policy={"monthly_day": 1, "clock": FIXED}),
                lambda doc: doc["reset_policy"].update(bimonthly_start_month=3),
                lambda doc: doc["reset_policy"].update(bimonthly_day=0),
                lambda doc: doc["reset_policy"].update(daily_fee_allocation="prorate_by_cycle_minutes"),
                lambda doc: doc["opening"].update(cycle_end_utc_ms=mid),
                lambda doc: doc["plan"].update(tiers="progressive"),
                lambda doc: doc["plan"].update(rounding="meter_import_half_hour_kwh_half_even_2"),
                lambda doc: doc["plan"]["revisions"][0].update(charge_per_civil_day="1"),
                lambda doc: doc["plan"]["revisions"][0].update(time_mode="calendar"),
            ):
                bad = copy.deepcopy(config)
                update(bad)
                save(rates, bad)
                save(meter, register([(begin, "0"), (end, "202")]))
                review(end, code=3, error="invalid")

            rates.unlink()
            meter.unlink()
            assert "Cycle estimate at closing 404.000000 USD" in run("--open-review", "extended.cmr")
        print(f"Two-month tariff: {count} ordinary meter-bill invocations passed")


if __name__ == "__main__":
    main(*(Path(path).resolve(strict=True) for path in sys.argv[1:]))
