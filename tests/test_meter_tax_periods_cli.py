"""Review a tax change inside a billing cycle with the installed meter-bill CLI."""
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
from test_meter_tax_cli import UTC, fraction, period, tax_tariff, unit


def segmented(value, *periods):
    tax_tariff(value, *periods)
    value["schema_version"] = 16
    value["confirmed_utc"] = value["opening"]["cycle_end_utc_ms"] // 1000
    value["plan"]["tax_allocation"] = "service_and_cycle_close"
    del value["opening"]["tax_bases"]
    value["opening"]["tax_segments"] = []
    return value


def segment(begin, measured, energy, daily="0", cycle="0", billed="0"):
    return dict(begin_utc_ms=begin, imported_kwh=measured, energy=energy,
                daily_fee=daily, cycle_fee=cycle, billed_kwh=billed)


def main(cli, bundle=None):
    with TemporaryDirectory(prefix="canary-tax-periods-") as tmp:
        root = Path(tmp)
        if bundle:
            installed = root / "installed \u03a9"
            installed.mkdir()
            cli = Path(shutil.copy2(cli, installed / cli.name))
            shutil.copytree(bundle, installed / BUNDLE)
        tariff, meter = root / "private policy.json", root / "private intervals.json"
        checked = 0

        def run(*args, error=None):
            nonlocal checked
            result = subprocess.run([cli, "meter-bill", *map(str, args)], cwd=root,
                                    capture_output=True, text=True, timeout=20)
            assert all(value not in result.stdout + result.stderr for value in (SCOPE, tariff.name, meter.name))
            if error:
                assert result.returncode == 3 and not result.stdout and error in result.stderr, result
            else:
                assert result.returncode == 0 and result.stdout and not result.stderr, result
            checked += 1
            return result.stdout

        def review(policy, readings, cutoff, *args, error=None):
            tariff.write_text(json.dumps(policy, separators=(",", ":")), encoding="ascii")
            meter.write_text(json.dumps(readings), encoding="ascii")
            return run("--tariff", tariff, "--intervals", meter, "--as-of-utc-ms", cutoff, *args, error=error)

        if bundle:
            begin = utc("2024-11-03T04:00:00")
            middle = begin + 2 * HOUR
            end = begin + 25 * HOUR
            policy = segmented(plan(begin, begin, end, named("America/New_York")),
                period(begin, middle, fraction("vat", "0.1", ("energy", "daily_fee")), published=begin),
                period(middle, end, fraction("vat", "0.2", ("energy", "daily_fee")), published=begin))
            policy["plan"]["revisions"][0]["charge_per_civil_day"] = "3"
            total = review(policy, interval(begin, end), end, "--save-review", "repeated.cmr")
            assert "Tax vat added 0.500000 USD on 5.000000 USD" in total
            assert "Tax vat added 4.600000 USD on 23.000000 USD" in total
            assert "Cycle estimate at closing 33.100000 USD" in total
            assert total == run("--open-review", "repeated.cmr")
        else:
            begin = utc("2024-01-01T23:00:00")
            middle, end = begin + HOUR, begin + 2 * HOUR
            first = period(begin, middle,
                unit("excise", "1"), fraction("vat", "0.1", ("energy", "daily_fee", "cycle_fee"), ("excise",)))
            second = period(middle, end,
                unit("excise", "2"), fraction("vat", "0.2", ("energy", "daily_fee", "cycle_fee"), ("excise",)))
            policy = segmented(plan(begin, begin, end, UTC), first, second)
            policy["plan"]["cycle_charge"] = "1"
            policy["plan"]["revisions"][0].update(base_rate_per_kwh="5", charge_per_civil_day="2")
            readings = register([(begin, "0"), (middle, "1"), (end, "2")])
            total = review(policy, readings, end, "--save-review", "rate-change.cmr")
            assert "Tax vat added 0.900000 USD on 9.000000 USD" in total
            assert "Tax vat added 1.800000 USD on 9.000000 USD" in total
            assert "Cycle estimate at closing 20.700000 USD" in total
            report = review(policy, interval(begin, end), end)
            assert "Cycle estimate at closing 20.700000 USD" in report
            assert run("--open-review", "rate-change.cmr") == total
            blob = (root / "rate-change.cmr").read_bytes()
            changed = blob[:-65].replace(b'"kind":"fraction","rate":"0.2"', b'"kind":"fraction","rate":"0.3"')
            assert changed != blob[:-65]
            (root / "rate-change.cmr").write_bytes(changed + sha256(changed).hexdigest().encode("ascii") + b"\n")
            run("--open-review", "rate-change.cmr", error="differ from the recalculated amounts")

            unit_policy = segmented(plan(begin, begin, end, UTC),
                period(begin, middle, unit("vat", "0.1")), period(middle, end, unit("vat", "0")))
            report = review(unit_policy, readings, end, "--save-review", "basis.cmr")
            assert "Tax vat added 0.100000 USD on 1.000000 kWh" in report
            archive = root / "basis.cmr"
            header, cutoff, length, payload = archive.read_bytes()[:-65].split(b"\n", 3)
            size = int(length)
            declaration = json.loads(payload[:size])
            declaration["plan"]["tax_periods"][0]["rules"] = [fraction("vat", "0.1", ("energy",))]
            changed = json.dumps(declaration, separators=(",", ":")).encode("ascii")
            body = b"\n".join((header, cutoff, str(len(changed)).encode("ascii"), changed + payload[size:]))
            archive.write_bytes(body + sha256(body).hexdigest().encode("ascii") + b"\n")
            run("--open-review", "basis.cmr", error="differ from the recalculated amounts")

            partial = review(policy, register([(begin, "0"), (middle, "1")]), middle, "--save-review", "prefix.cmr")
            assert "Provisional cycle estimate 9.900000 USD" in partial
            assert f"Tax period from {middle}" not in partial
            meter.write_text(json.dumps(readings), encoding="ascii")
            extended = run("--extend-review", "prefix.cmr", "--intervals", meter, "--as-of-utc-ms", end,
                           "--save-review", "complete.cmr")
            assert "Cycle estimate at closing 20.700000 USD" in extended
            assert run("--open-review", "complete.cmr") == extended
            review(policy, register([(begin, "0"), (end, "2")]), end, error="crosses a tax period boundary")
            review(policy, register([(begin, "0"), (end, "0")]), end)

            opened = copy.deepcopy(policy)
            opened["opening"].update(through_utc_ms=middle, imported_kwh="1", accrued_subtotal="8",
                                     tax_segments=[segment(begin, "1", "5", "2", "1")])
            report = review(opened, interval(middle, end), end)
            assert "Change from imported readings 10.800000 USD" in report
            assert "Cycle estimate at closing 20.700000 USD" in report
            shifted = copy.deepcopy(opened)
            shifted["opening"]["tax_segments"][0].update(energy="7", daily_fee="0")
            review(shifted, interval(middle, end), end, error="invalid")
            altered = copy.deepcopy(opened)
            altered["opening"]["tax_segments"][0]["begin_utc_ms"] += 1
            review(altered, interval(middle, end), end, error="invalid")

            later = end + HOUR
            reallocated = segmented(plan(begin, begin, later, UTC),
                period(begin, middle, fraction("vat", "0", ("energy",))),
                period(middle, later, fraction("vat", "1", ("energy",))))
            reallocated["plan"]["revisions"][0]["base_rate_per_kwh"] = "5"
            reallocated["opening"].update(through_utc_ms=end, imported_kwh="2", accrued_subtotal="10",
                tax_segments=[segment(begin, "1", "5"), segment(middle, "1", "5")])
            report = review(reallocated, interval(end, later), later)
            assert "Cycle estimate at closing 25.000000 USD" in report
            reallocated["opening"]["tax_segments"][0]["energy"] = "9"
            reallocated["opening"]["tax_segments"][1]["energy"] = "1"
            review(reallocated, interval(end, later), later, error="invalid")

            half = begin + HOUR // 2
            dynamic = copy.deepcopy(reallocated)
            rates = [dict(begin_utc_ms=a, end_utc_ms=b, published_utc_ms=begin, rate_per_kwh=rate)
                     for a, b, rate in [(begin, half, "5"), (half, middle, "6"), (middle, later, "5")]]
            dynamic["plan"]["revisions"][0].update(time_mode="dynamic", base_rate_per_kwh="0", dynamic=rates)
            dynamic["opening"].update(accrued_subtotal="10.5",
                tax_segments=[segment(begin, "1", "5.5"), segment(middle, "1", "5")])
            report = review(dynamic, interval(end, later), later)
            assert "Cycle estimate at closing 25.500000 USD" in report
            dynamic["opening"]["accrued_subtotal"] = "9"
            dynamic["opening"]["tax_segments"][0]["energy"] = "4"
            review(dynamic, interval(end, later), later, error="invalid")

            finish = later + HOUR
            progressive = segmented(plan(begin, begin, finish, UTC),
                period(begin, end, fraction("vat", "0", ("energy",))),
                period(end, finish, fraction("vat", "1", ("energy",))))
            progressive["plan"]["tiers"] = "progressive"
            revisions = progressive["plan"]["revisions"]
            second_rate = copy.deepcopy(revisions[0])
            revisions[0].update(end_utc_ms=middle, base_rate_per_kwh="5",
                                bands=[dict(through_kwh=None, rate_per_kwh="1")])
            second_rate.update(key="adjusted", begin_utc_ms=middle, base_rate_per_kwh="1",
                               bands=[dict(through_kwh=None, rate_per_kwh="5")])
            revisions.append(second_rate)
            progressive["opening"].update(through_utc_ms=later, imported_kwh="2", accrued_subtotal="12",
                tax_segments=[segment(begin, "1", "6"), segment(end, "1", "6")])
            report = review(progressive, interval(later, finish), finish)
            assert "Cycle estimate at closing 30.000000 USD" in report
            progressive["opening"]["tax_segments"][0]["energy"] = "9"
            progressive["opening"]["accrued_subtotal"] = "15"
            review(progressive, interval(later, finish), finish, error="invalid")

            gap = copy.deepcopy(policy)
            gap["plan"]["tax_periods"][1]["begin_utc_ms"] += 1
            review(gap, readings, end, error="saved tariff declaration unavailable or invalid")
            overlap = copy.deepcopy(policy)
            overlap["plan"]["tax_periods"][1]["begin_utc_ms"] -= 1
            review(overlap, readings, end, error="saved tariff declaration unavailable or invalid")
            future = copy.deepcopy(policy)
            future["plan"]["tax_periods"][1]["published_utc_ms"] = end
            report = review(future, register([(begin, "0"), (middle, "1")]), middle)
            assert "Provisional cycle estimate 9.900000 USD" in report
            review(future, interval(begin, middle + HOUR // 2), middle + HOUR // 2, error="published after decision")

            rounded = segmented(plan(begin, begin, end, UTC),
                period(begin, middle, unit("excise", "1", basis="billed_kwh")),
                period(middle, end, unit("excise", "2", basis="billed_kwh")))
            rounded["plan"]["rounding"] = "meter_import_half_hour_kwh_half_even_2"
            rounded_readings = register([(begin, "0"), (begin + HOUR // 2, "0.005"),
                (middle, "0.02"), (middle + HOUR // 2, "0.045"), (end, "0.051")])
            report = review(rounded, rounded_readings, end)
            assert "Tax excise added 0.020000 USD on 0.020000 kWh billed" in report
            assert "Tax excise added 0.060000 USD on 0.030000 kWh billed" in report
            assert "Cycle estimate at closing 0.130000 USD" in report
            rounded["plan"]["tax_periods"][0]["end_utc_ms"] += 1
            rounded["plan"]["tax_periods"][1]["begin_utc_ms"] += 1
            review(rounded, rounded_readings, end, error="saved tariff declaration unavailable or invalid")

            charges = ("demand", "minimum_adjustment")
            close = segmented(demand_tariff(begin, end),
                period(begin, middle, fraction("settlement", "1", charges)),
                period(middle, end, fraction("settlement", "0.1", charges)))
            close["plan"]["minimum_cycle_subtotal"] = "12"
            report = review(close, interval(begin, middle), middle)
            assert "Tax settlement added 0.000000 USD" in report
            assert "Provisional cycle estimate 6.000000 USD" in report
            report = review(close, interval(begin, end), end)
            assert "Tax settlement added 1.000000 USD on 10.000000 USD" in report
            assert "Cycle estimate at closing 13.000000 USD" in report
            close["opening"].update(through_utc_ms=middle, imported_kwh="1", accrued_subtotal="1",
                                    peak_demand_kw="1", tax_segments=[segment(begin, "1", "1")])
            report = review(close, interval(middle, end), end)
            assert "Change from imported readings 7.000000 USD" in report
            assert "Cycle estimate at closing 13.000000 USD" in report

            repriced = segmented(plan(begin, begin, end, UTC),
                period(begin, middle, fraction("vat", "0.1", ("energy",))),
                period(middle, end, fraction("vat", "0.2", ("energy",))))
            repriced["plan"]["tiers"] = "all_units"
            repriced["plan"]["revisions"][0]["bands"] = [
                dict(through_kwh="1", rate_per_kwh="1"), dict(through_kwh=None, rate_per_kwh="3")]
            report = review(repriced, readings, end)
            assert "Tax vat added 0.400000 USD on 4.000000 USD" in report
            assert "Tax vat added 0.800000 USD on 4.000000 USD" in report
            assert "Cycle estimate at closing 9.200000 USD" in report
            repriced["opening"].update(through_utc_ms=middle, imported_kwh="1", accrued_subtotal="2",
                                       tax_segments=[segment(begin, "1", "2")])
            report = review(repriced, interval(middle, end), end)
            assert "Change from imported readings 7.000000 USD" in report
            assert "Cycle estimate at closing 9.200000 USD" in report
        print(f"Tax period allocation: {checked} meter-bill CLI cases passed")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None)
