"""Apply a factor to one part of the delivered unit rate through ordinary meter review."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

from test_meter_bill_named_cli import BUNDLE, HOUR, SCOPE, calendar, named, plan, register, utc

UTC = {"kind": "utc", "fixed_offset_seconds": 0}


def declaration(begin, end, clock=UTC):
    value = plan(begin, begin, end, clock)
    value["schema_version"] = 8
    calendar(value, 60, 120)
    revision = value["plan"]["revisions"][0]
    revision.update(base_rate_per_kwh="3", charge_per_civil_day="0",
                    component_multiplier={"rate_per_kwh": "2", "base_multiplier": "1"})
    revision["calendar"][0]["multiplier"] = "2"
    return value


def main(cli, bundle=None):
    with TemporaryDirectory(prefix="canary-component-rate-") as folder:
        work = Path(folder)
        if bundle:
            installed = work / "installed \u03a9"
            installed.mkdir()
            cli = Path(shutil.copy2(cli, installed / cli.name))
            shutil.copytree(bundle, installed / BUNDLE)
        policy = work / "private tariff.json"
        meter = work / "private meter.json"
        checks = 0

        def write(path, value):
            path.write_text(json.dumps(value), encoding="ascii")

        def run(*args, error=None):
            nonlocal checks
            result = subprocess.run([cli, "meter-bill", *map(str, args)], cwd=work,
                                    capture_output=True, text=True, timeout=15)
            assert all(text not in result.stdout + result.stderr for text in (SCOPE, policy.name, meter.name))
            if error:
                assert result.returncode == 3 and not result.stdout and error in result.stderr, result
            else:
                assert result.returncode == 0 and result.stdout and not result.stderr, result
            checks += 1
            return result.stdout

        def review(value, points, at=None, archive=None, error=None):
            write(policy, value)
            write(meter, register(points))
            arguments = ["--tariff", policy, "--intervals", meter, "--as-of-utc-ms", at or points[-1][0]]
            if archive:
                arguments += ["--save-review", archive]
            return run(*arguments, error=error)

        def closing(amount, text):
            assert f"Cycle estimate at closing {amount} USD" in text, text

        begin = utc("2024-01-01")
        off, end = begin + HOUR, begin + 2 * HOUR
        options = declaration(begin, end)
        points = [(begin, "0"), (off, "0"), (end, "1")]
        if bundle:
            # The New York 01:00 slot occurs twice. Both local hours apply the same factor.
            begin = utc("2024-11-03T04:00:00")
            end = begin + 4 * HOUR
            options = declaration(begin, end, named("America/New_York"))
            points = [(begin, "0"), (begin + HOUR, "1"), (begin + 3 * HOUR, "3"), (end, "4")]
            report = review(options, points, archive="named.cmr")
            closing("24.000000", report)
            assert "Reviewed IANA time database 2026e" in report
            assert run("--open-review", "named.cmr") == report
        else:
            # T24: the peak factor multiplies supply 2, then the network rate 3 is added.
            report = review(options, points, archive="t24.cmr")
            closing("7.000000", report)
            assert "Component multipliers apply to the declared part of the unit rate." in report
            assert b'"component_multiplier": {"rate_per_kwh": "2", "base_multiplier": "1"}' in (work / "t24.cmr").read_bytes()
            policy.unlink()
            meter.unlink()
            assert run("--open-review", "t24.cmr") == report
            closing("12.000000", review(options, [(begin, "0"), (off, "1"), (end, "2")]))

            fixed = copy.deepcopy(options)
            revision = fixed["plan"]["revisions"][0]
            revision.update(time_mode="constant", calendar=[], charge_per_civil_day="4")
            revision["component_multiplier"]["base_multiplier"] = "2"
            fixed["plan"].update(cycle_charge="1", subtotal_multiplier="1.1")
            closing("13.200000", review(fixed, [(begin, "0"), (end, "1")]))

            announced = copy.deepcopy(options)
            announcement = off + HOUR // 2
            announced["confirmed_utc"] = announcement // 1000
            announced["plan"]["revisions"][0]["calendar"][0]["published_utc_ms"] = announcement
            early = review(announced, [(begin, "0"), (off, "0"), (announcement - 1, "1")])
            assert "Provisional cycle estimate 5.000000 USD" in early
            closing("7.000000", review(announced, points))

            dynamic = copy.deepcopy(options)
            revision = dynamic["plan"]["revisions"][0]
            revision.update(time_mode="dynamic", base_rate_per_kwh="0", calendar=[], dynamic=[
                {"begin_utc_ms": begin, "end_utc_ms": off, "published_utc_ms": begin,
                 "rate_per_kwh": "3", "multiplier": "0"},
                {"begin_utc_ms": off, "end_utc_ms": end, "published_utc_ms": begin,
                 "rate_per_kwh": "1", "multiplier": "3"},
            ])
            revision["component_multiplier"] = {"rate_per_kwh": "-2", "base_multiplier": "0"}
            closing("-2.000000", review(dynamic, [(begin, "0"), (off, "1"), (end, "2")]))
            revision["dynamic"][1]["published_utc_ms"] = off
            dynamic["confirmed_utc"] = off // 1000
            first = review(dynamic, [(begin, "0"), (off - 1, "1")])
            assert "Provisional cycle estimate 3.000000 USD" in first
            revision["dynamic"][0]["end_utc_ms"] = off - 1
            review(dynamic, points, error="dynamic price gap")

            precision = copy.deepcopy(options)
            rule = precision["plan"]["revisions"][0]
            rule["component_multiplier"]["rate_per_kwh"] = "0.00000025"
            # The products differ by 10^-18 on both sides of the six-place half-even tie.
            for factor, expected in (("1.999999999996", "3.000000"), ("2", "3.000000"),
                                     ("2.000000000004", "3.000001")):
                rule["calendar"][0]["multiplier"] = factor
                closing(expected, review(precision, points))

            # Schema 8 retains the selected seasonal and two-month billing semantics.
            day_end = begin + 24 * HOUR
            seasonal = declaration(begin, day_end)
            seasonal["seasonal_allowance"] = {"published_utc_ms": begin, "seasons": [
                {"start_month": 1, "start_day": 1, "kwh_per_day": "2"}
            ]}
            seasonal["plan"].update(tiers="progressive")
            row = seasonal["plan"]["revisions"][0]
            row.update(time_mode="constant", calendar=[], bands=[
                {"through_kwh": None, "rate_per_kwh": "1"},
                {"through_kwh": None, "rate_per_kwh": "2"},
            ])
            row["component_multiplier"]["base_multiplier"] = "2"
            combined = review(seasonal, [(begin, "0"), (day_end, "3")])
            assert "Seasonal allowance for cycle 2 kWh" in combined
            closing("25.000000", combined)

            two_month_end = utc("2024-03-01")
            two_month = declaration(begin, two_month_end)
            two_month["reset_policy"] = {"bimonthly_start_month": 1, "bimonthly_day": 1, "clock": UTC}
            two_month["plan"].update(tiers="all_units")
            row = two_month["plan"]["revisions"][0]
            row.update(time_mode="constant", calendar=[], bands=[
                {"through_kwh": "100", "rate_per_kwh": "1"},
                {"through_kwh": None, "rate_per_kwh": "2"},
            ])
            row["component_multiplier"]["base_multiplier"] = "2"
            closing("1818.000000", review(two_month, [(begin, "0"), (two_month_end, "202")]))

            legacy = copy.deepcopy(options)
            legacy["schema_version"] = 1
            absent = "saved tariff declaration unavailable or invalid"
            review(legacy, points, error=absent)
            for change in (
                lambda row: row["calendar"][0].pop("multiplier"),
                lambda row: row["calendar"][0].update(multiplier="-1"),
                lambda row: row["calendar"][0].update(multiplier=2),
                lambda row: row["component_multiplier"].pop("base_multiplier"),
                lambda row: row.pop("component_multiplier"),
            ):
                invalid = copy.deepcopy(options)
                change(invalid["plan"]["revisions"][0])
                review(invalid, points, error=absent)
            legacy["plan"]["revisions"][0].pop("component_multiplier")
            legacy["plan"]["revisions"][0]["calendar"][0].pop("multiplier")
            ordinary = review(legacy, points)
            closing("3.000000", ordinary)
            assert "Component multipliers apply" not in ordinary
        print(f"Component multipliers: {checks} ordinary meter-bill invocations passed")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None)
