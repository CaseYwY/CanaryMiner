"""Exercise the seasonal allowance in the ordinary local meter review."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

from test_meter_bill_named_cli import BUNDLE, SCOPE, interval, named, plan, register, utc

UTC = {"kind": "utc", "fixed_offset_seconds": 0}


def declaration(begin, end, clock=UTC, policy="explicit_cycle_only", rates_end=None):
    value = plan(begin, begin, end, clock, policy, rates_end)
    value["schema_version"] = 7
    value["seasonal_allowance"] = {
        "published_utc_ms": begin - 1000,
        "seasons": [
            {"start_month": 1, "start_day": 1, "kwh_per_day": "2"},
            {"start_month": 5, "start_day": 1, "kwh_per_day": "3"},
        ],
    }
    value["plan"].update(tiers="progressive", minimum_cycle_subtotal=None)
    value["plan"]["revisions"][0].update(base_rate_per_kwh="0", bands=[
        {"through_kwh": None, "rate_per_kwh": "1"},
        {"through_kwh": None, "rate_per_kwh": "2"},
    ])
    return value


def main(cli, bundle=None):
    with TemporaryDirectory(prefix="canary-seasonal-meter-") as folder:
        work = Path(folder)
        if bundle:
            installed = work / "installed \u03a9"
            installed.mkdir()
            cli = Path(shutil.copy2(cli, installed / cli.name))
            shutil.copytree(bundle, installed / BUNDLE)
        rates, meter = work / "private allowance.json", work / "private meter.json"
        checks = 0

        def save(path, doc):
            path.write_text(json.dumps(doc), encoding="ascii")

        def run(*args, code=0, error=None):
            nonlocal checks
            result = subprocess.run([cli, "meter-bill", *map(str, args)], cwd=work,
                                    capture_output=True, text=True, timeout=20)
            assert result.returncode == code, result
            assert all(text not in result.stdout + result.stderr for text in (SCOPE, rates.name, meter.name))
            if code:
                assert not result.stdout and (not error or error in result.stderr), result
            else:
                assert result.stdout and not result.stderr, result
            checks += 1
            return result.stdout

        def review(at, *args, **kwargs):
            return run("--tariff", rates, "--intervals", meter, "--as-of-utc-ms", at, *args, **kwargs)

        if bundle:
            clock = named("America/New_York")
            begin, end = map(utc, ("2024-03-09T05:00:00", "2024-03-12T04:00:00"))
            options = declaration(begin, end, clock)
            options["seasonal_allowance"]["seasons"][1].update(start_month=3, start_day=10)
            save(rates, options)
            save(meter, register([(begin, "0"), (end, "10")]))
            report = review(end, "--save-review", "daylight.cmr")
            assert "Seasonal allowance for cycle 8 kWh" in report
            assert "Cycle estimate at closing 12.000000 USD" in report
            assert run("--open-review", "daylight.cmr") == report
            options["opening"]["cycle_end_utc_ms"] += 3600000
            save(rates, options)
            review(end, code=3, error="invalid")

            # Apia skipped December 30. Its resolved UTC boundary is also December 31's start.
            gap_start, gap_end, after_gap = map(utc, (
                "2011-12-29T10:00:00", "2011-12-30T10:00:00", "2011-12-31T10:00:00"))
            apia = named("Pacific/Apia")
            save(rates, declaration(gap_start, gap_end, apia))
            save(meter, register([(gap_start, "0"), (gap_end, "10")]))
            review(gap_end, code=3, error="invalid")
            save(rates, declaration(gap_end, after_gap, apia))
            save(meter, register([(gap_end, "0"), (after_gap, "10")]))
            assert "Seasonal allowance for cycle 3 kWh" in review(after_gap)

            february, march, april = map(utc, (
                "2024-02-01T05:00:00", "2024-03-01T05:00:00", "2024-04-01T05:00:00"))
            reset = {"monthly_day": 1, "clock": {"kind": "fixed_standard", "fixed_offset_seconds": -18000}}
            save(rates, declaration(february, march, clock, reset, april))
            save(meter, register([(february, "0"), (march, "10"), (april, "20")]))
            review(april, code=3, error="seasonal allowance requires complete tariff civil days")

            # The full-cycle allowance cannot depend on a date that reappears after midnight.
            start, prefix_end, closing = map(utc, (
                "2010-11-01T02:30:00", "2010-11-02T02:30:00", "2010-11-08T03:30:00"))
            save(rates, declaration(start, closing, named("America/St_Johns")))
            save(meter, register([(start, "0"), (prefix_end, "10")]))
            review(prefix_end, code=3, error="invalid")
            (installed / BUNDLE / "version").write_text("different", encoding="ascii")
            run("--open-review", "daylight.cmr", code=3, error="time database unavailable or invalid")
        else:
            begin, change, end, next_end = map(utc, ("2024-04-21", "2024-05-01", "2024-05-21", "2024-06-21"))
            options = declaration(begin, end, policy={"monthly_day": 21, "clock": UTC}, rates_end=next_end)
            save(rates, options)
            save(meter, register([(begin, "0"), (change, "60")]))
            before = review(change, "--save-review", "prefix.cmr")
            assert "Seasonal allowance for cycle 80 kWh" in before
            assert "Provisional cycle estimate 60.000000 USD" in before
            prefix_bytes = (work / "prefix.cmr").read_bytes()
            save(meter, register([(begin, "0"), (change, "60"), (end, "100"), (next_end, "200")]))
            result = run("--extend-review", "prefix.cmr", "--intervals", meter, "--as-of-utc-ms",
                         next_end, "--save-review", "extended.cmr")
            assert "Seasonal allowance for cycle 80 kWh" in result
            assert "Cycle estimate at closing 120.000000 USD" in result
            assert "Seasonal allowance for cycle 93 kWh" in result
            assert "Cycle estimate at closing 107.000000 USD" in result
            assert (work / "prefix.cmr").read_bytes() == prefix_bytes
            assert run("--open-review", "extended.cmr") == result

            save(meter, register([(begin, "0"), (next_end, "200")]))
            review(next_end, code=3, error="needs a reading at the billing-cycle boundary")
            save(meter, interval(begin, end, "100"))
            out = review(end)
            assert "Imported usage 72.000000 kWh" in out and "Cycle estimate at closing 72.000000 USD" in out

            revised = copy.deepcopy(options)
            first = revised["plan"]["revisions"][0]
            first["end_utc_ms"] = change
            revised["plan"]["revisions"].append({
                **copy.deepcopy(first), "key": "synthetic-separate-rate", "begin_utc_ms": change, "end_utc_ms": next_end,
            })
            revised["plan"]["revisions"][1]["bands"][1]["rate_per_kwh"] = "4"
            save(rates, revised)
            save(meter, register([(begin, "0"), (change, "90"), (end, "100")]))
            assert "Cycle estimate at closing 140.000000 USD" in review(end)
            save(meter, register([(begin, "0"), (end, "100")]))
            review(end, code=3)

            opened = copy.deepcopy(options)
            opened["opening"].update(through_utc_ms=change, imported_kwh="90", accrued_subtotal="100")
            save(rates, opened)
            save(meter, register([(change, "90"), (end, "100")]))
            report = review(end)
            assert "Change from imported readings 20.000000 USD" in report
            assert "Cycle estimate at closing 120.000000 USD (includes declared opening)" in report

            late = copy.deepcopy(options)
            late["confirmed_utc"] = end // 1000
            late["seasonal_allowance"]["published_utc_ms"] = change + 1
            save(rates, late)
            save(meter, register([(begin, "0"), (change, "60")]))
            review(change, code=3, error="rate published after decision instant")

            start, finish = map(utc, ("2024-01-01", "2024-01-03"))
            winter = declaration(start, finish)
            winter["seasonal_allowance"]["seasons"] = [
                {"start_month": 5, "start_day": 1, "kwh_per_day": "3"},
                {"start_month": 11, "start_day": 1, "kwh_per_day": "2"},
            ]
            save(rates, winter)
            save(meter, register([(start, "0"), (finish, "10")]))
            assert "Cycle estimate at closing 16.000000 USD" in review(finish)
            winter["seasonal_allowance"]["seasons"][1]["kwh_per_day"] = "0"
            save(rates, winter)
            out = review(finish)
            assert "Seasonal allowance for cycle 0 kWh" in out
            assert "Cycle estimate at closing 20.000000 USD" in out

            for patch in (
                lambda doc: doc.update(schema_version=5),
                lambda doc: doc.pop("seasonal_allowance"),
                lambda doc: doc["seasonal_allowance"]["seasons"].reverse(),
                lambda doc: doc["seasonal_allowance"]["seasons"][1].update(start_month=1),
                lambda doc: doc["seasonal_allowance"]["seasons"][1].update(start_month=2, start_day=29),
                lambda doc: doc["seasonal_allowance"]["seasons"][0].update(kwh_per_day="-1"),
                lambda doc: doc["seasonal_allowance"].update(published_utc_ms=begin + 1000),
                lambda doc: doc["plan"].update(tiers="all_units"),
                lambda doc: doc["plan"].update(rounding="meter_import_half_hour_kwh_half_even_2"),
                lambda doc: doc["plan"]["revisions"][0]["bands"][0].update(through_kwh="80"),
                lambda doc: doc["opening"].update(cycle_end_utc_ms=end - 1),
            ):
                invalid = copy.deepcopy(options)
                patch(invalid)
                save(rates, invalid)
                save(meter, register([(begin, "0"), (end, "100")]))
                review(end, code=3, error="invalid")

            rates.unlink()
            meter.unlink()
            (work / "prefix.cmr").unlink()
            assert run("--open-review", "extended.cmr") == result

        print(f"Seasonal allowance: {checks} ordinary meter-bill invocations passed")


if __name__ == "__main__":
    main(*(Path(path).resolve(strict=True) for path in sys.argv[1:]))
