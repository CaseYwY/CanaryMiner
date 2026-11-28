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

def eligible(begin, end, weekdays=127, months=4095, date=None, included=True):
    return dict(begin_minute=begin, end_minute=end, weekday_mask=weekdays, month_mask=months,
                date=date, eligible=included)

def concurrent(value, second_rule):
    value = copy.deepcopy(value)
    value["schema_version"] = 18
    value["demand_charges"] = [
        {"key": "private-facility", "rule": value.pop("demand_charge")},
        {"key": "private-time", "rule": copy.deepcopy(second_rule)},
    ]
    value["opening"]["demand_peaks"] = [
        {"key": "private-facility", "peak": value["opening"].pop("peak_demand_kw")},
        {"key": "private-time", "peak": "0"},
    ]
    return value

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
            both = concurrent(policy, dict(policy["demand_charge"], rate_per_kw="3",
                                           eligible_periods=[eligible(60, 120)]))
            report = review(both, readings, end, archive="concurrent-fold.cmr")
            totals(report, "8.000000", "77.000000")
            assert "Combined demand charge before subtotal multiplier 64.000000 USD" in report
            assert run("--open-review", "concurrent-fold.cmr") == report
            policy["schema_version"] = 17
            policy["demand_charge"]["eligible_periods"] = [eligible(60, 120)]
            # Both occurrences of 01:00 are eligible. The following local hour is excluded.
            folded = register([(begin, "0"), (begin + HOUR, "1"), (begin + 2 * HOUR, "5"),
                               (begin + 3 * HOUR, "13"), (end, "40")])
            report = review(policy, folded, end, archive="fold.cmr")
            totals(report, "8.000000", "80.000000")
            assert run("--open-review", "fold.cmr") == report

            spring = utc("2024-03-10T06:00:00")
            spring_end = spring + 2 * HOUR
            policy = declaration(spring, spring_end, named("America/New_York"), minutes=60)
            policy["schema_version"] = 17
            policy["demand_charge"]["eligible_periods"] = [eligible(120, 180)]
            # The complete local 02:00 hour does not exist.
            totals(review(policy, register([(spring, "0"), (spring_end, "30")]), spring_end),
                   "0.000000", "30.000000")
            policy["demand_charge"]["eligible_periods"] = [eligible(120, 210)]
            review(policy, modeled((spring, spring_end, "4000")), spring_end, error="saved tariff declaration")
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

            # A v9 source can resolve demand and energy on different grids.
            quarter_hour = declaration(begin, rounded_end)
            quarter_hour["plan"]["rounding"] = "meter_import_half_hour_kwh_half_even_2"
            quarters = register([(begin + i * HOUR // 4, amount) for i, amount in
                                 enumerate(("0", "0.005", "0.01", "0.015", "0.02"))])
            report = review(quarter_hour, quarters, rounded_end, archive="both-grids.cmr")
            totals(report, "0.020000", "0.120000")
            assert "Billed usage 0.020000 kWh" in report
            assert run("--open-review", "both-grids.cmr") == report
            review(quarter_hour, register([(begin, "0"), (halfway, "0.01"), (rounded_end, "0.02")]),
                   rounded_end, error=boundary)
            twenty = declaration(begin, rounded_end, minutes=20)
            twenty["plan"]["rounding"] = "meter_import_half_hour_kwh_half_even_2"
            shared = [(begin + i * 10 * MINUTE, amount) for i, amount in
                      enumerate(("0", "0.005", "0.01", "0.015", "0.02", "0.025", "0.03"))]
            report = review(twenty, register(shared), rounded_end)
            totals(report, "0.030000", "0.190000")
            assert "Billed usage 0.040000 kWh" in report
            rounding_boundary = "meter rounding needs readings and cycle boundaries on the UTC half hour"
            review(twenty, register(shared[:3] + shared[4:]), rounded_end, error=rounding_boundary)
            zero_gap = register([(begin, "0"), (begin + 10 * MINUTE, "0.005"), (rounded_end, "0.005")])
            totals(review(twenty, zero_gap, rounded_end), "0.015000", "0.075000")
            legacy_rounding = copy.deepcopy(quarter_hour)
            legacy_rounding["schema_version"] = 1
            legacy_rounding.pop("demand_charge")
            legacy_rounding["opening"].pop("peak_demand_kw")
            review(legacy_rounding, quarters, rounded_end, error=rounding_boundary)

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
            # Monday only, January only. Two aligned eligible windows from 00:30 to 01:00.
            start_peak = begin + 30 * MINUTE
            end_peak = begin + HOUR
            after = begin + 2 * HOUR
            scheduled = declaration(begin, after)
            scheduled["schema_version"] = 17
            scheduled["demand_charge"]["eligible_periods"] = [eligible(30, 60, 1, 1)]
            readings = register([(begin, "0"), (start_peak, "8"), (start_peak + 15 * MINUTE, "9"),
                                 (end_peak, "11"), (after, "31")])
            report = review(scheduled, readings, after, archive="scheduled.cmr")
            totals(report, "8.000000", "71.000000")
            assert "Demand uses only windows in the declared eligibility periods" in report
            assert run("--open-review", "scheduled.cmr") == report

            # Uniform-power intervals can cross the boundary, unlike a nonzero register delta.
            report = review(scheduled, modeled((begin, after, "4000")), after)
            totals(report, "4.000000", "28.000000")
            apparent = copy.deepcopy(scheduled)
            apparent["demand_charge"]["rate_per_kva"] = apparent["demand_charge"].pop("rate_per_kw")
            apparent["demand_charge"]["power_factor_va_places"] = None
            apparent["opening"]["peak_demand_kva"] = apparent["opening"].pop("peak_demand_kw")
            apparent_load = modeled((begin, after, "4000"))
            apparent_load["schema_version"] = 4
            apparent_load["intervals"][0]["average_va"] = "6000"
            report = review(apparent, apparent_load, after)
            assert "Peak demand 6.000000 kVA" in report and "Cycle estimate at closing 38.000000 USD" in report
            review(scheduled, register([(begin, "0"), (end_peak, "11"), (after, "31")]), after,
                   error=boundary)

            holiday = copy.deepcopy(scheduled)
            holiday["demand_charge"]["eligible_periods"].append(
                eligible(0, 1440, date="2024-01-01", included=False))
            totals(review(holiday, register([(begin, "0"), (after, "31")]), after),
                   "0.000000", "31.000000")
            redundant = copy.deepcopy(scheduled)
            redundant["demand_charge"]["eligible_periods"].append(eligible(31, 47, 1, 1))
            totals(review(redundant, readings, after), "8.000000", "71.000000")
            out_of_month = copy.deepcopy(scheduled)
            out_of_month["demand_charge"]["eligible_periods"] = [eligible(30, 60, 1, 2)]
            totals(review(out_of_month, register([(begin, "0"), (after, "31")]), after),
                   "0.000000", "31.000000")

            unaligned = copy.deepcopy(scheduled)
            unaligned["demand_charge"]["eligible_periods"] = [eligible(31, 60)]
            review(unaligned, readings, after, error="saved tariff declaration")
            excluded_opening = copy.deepcopy(scheduled)
            excluded_opening["opening"].update(through_utc_ms=start_peak, imported_kwh="8",
                                               accrued_subtotal="8", peak_demand_kw="1")
            review(excluded_opening, modeled((start_peak, after, "4000")), after,
                   error="saved tariff declaration")
            excluded_opening["opening"]["peak_demand_kw"] = "0"
            totals(review(excluded_opening, modeled((start_peak, after, "4000")), after),
                   "4.000000", "34.000000")
            full_opening = copy.deepcopy(excluded_opening)
            full_opening["demand_charge"]["eligible_periods"] = [eligible(0, 1440)]
            review(full_opening, modeled((start_peak, after, "4000")), after, error="saved tariff declaration")

            history = declaration(begin, feb, reset={"monthly_day": 1, "clock": UTC})
            history["schema_version"] = 17
            history["plan"]["revisions"][0]["base_rate_per_kwh"] = "0"
            history["demand_charge"].update(eligible_periods=[eligible(0, 1440, months=1)],
                                            ratchet={"lookback_months": 1, "fraction": "0.8"})
            history["opening"]["demand_history"] = [{"cycle_end_utc_ms": begin, "peak_kw": "5"}]
            review(history, modeled((begin, feb, "1000")), feb, error="saved tariff declaration")
            history["opening"]["demand_history"][0]["peak_kw"] = "0"
            totals(review(history, modeled((begin, feb, "1000")), feb), "1.000000", "5.000000")

            close = begin + HOUR
            quarters = [begin + i * 15 * MINUTE for i in range(5)]
            multiple = concurrent(declaration(begin, close, minutes=30), dict(
                window_minutes=15, offset_minutes=0, published_utc_ms=begin, rate_per_kw="3",
                eligible_periods=[eligible(15, 45)]))
            quarterly = modeled(*zip(quarters, quarters[1:], ("12000", "4000", "2000", "10000")))
            register_points = list(zip(quarters, ("0", "3", "4", "4.5", "7")))
            report = review(multiple, quarterly, close, archive="concurrent.cmr")
            totals(report, "8.000000", "59.000000")
            assert "Peak demand 4.000000 kW" in report and "Imported usage 7.000000 kWh" in report
            assert "Combined demand charge before subtotal multiplier 52.000000 USD" in report
            assert "private-facility" not in report and "private-time" not in report
            assert run("--open-review", "concurrent.cmr") == report
            cumulative = review(multiple, register(register_points), close)
            assert cumulative[cumulative.index("\nCycle "):] == report[report.index("\nCycle "):]
            review(multiple, register(register_points[:1] + register_points[2:]), close, error=boundary)

            existing = copy.deepcopy(multiple)
            existing["opening"].update(through_utc_ms=quarters[2], imported_kwh="4", accrued_subtotal="4",
                                       demand_peaks=[{"key": "private-time", "peak": "4"},
                                                     {"key": "private-facility", "peak": "8"}])
            report = review(existing, register(register_points[2:]), close)
            totals(report, "8.000000", "59.000000")
            assert "Change from imported readings 3.000000 USD" in report
            contradicts = copy.deepcopy(existing)
            contradicts["demand_charges"][0]["rule"]["window_minutes"] = 15
            contradicts["opening"]["demand_peaks"][0]["peak"] = "9"
            review(contradicts, register(register_points[2:]), close, error="saved tariff declaration")
            taxed = copy.deepcopy(existing)
            taxed["plan"]["tax_periods"] = [dict(
                begin_utc_ms=begin, end_utc_ms=close, published_utc_ms=begin,
                rules=[dict(key="tax-demand", kind="fraction", rate="0.1",
                            components=["demand"], included_in=[], prior_taxes=[])])]
            taxed["opening"]["tax_bases"] = dict(energy="4", daily_fee="0", cycle_fee="0", demand="52", billed_kwh="0")
            report = review(taxed, register(register_points[2:]), close, archive="concurrent-tax.cmr")
            assert "Cycle estimate at closing 64.200000 USD" in report
            assert run("--open-review", "concurrent-tax.cmr") == report
            taxed["opening"]["tax_bases"]["demand"] = "40"
            review(taxed, register(register_points[2:]), close, error="saved tariff declaration")

            mixed = copy.deepcopy(multiple)
            second = mixed["demand_charges"][1]["rule"]
            second["rate_per_kva"] = second.pop("rate_per_kw")
            second["power_factor_va_places"] = None
            review(mixed, quarterly, close, error="apparent")
            apparent_quarters = copy.deepcopy(quarterly)
            apparent_quarters["schema_version"] = 4
            for row, va in zip(apparent_quarters["intervals"], ("16000", "6000", "3500", "15000")):
                row["average_va"] = va
            report = review(mixed, apparent_quarters, close)
            assert "Peak demand 6.000000 kVA" in report and "Cycle estimate at closing 65.000000 USD" in report
            march = utc("2024-03-01")
            monthly = concurrent(declaration(begin, feb, reset={"monthly_day": 1, "clock": UTC}, rates_end=march),
                                 multiple["demand_charges"][1]["rule"])
            monthly["plan"]["revisions"][0]["base_rate_per_kwh"] = "0"
            for item, state, peak in zip(monthly["demand_charges"], monthly["opening"]["demand_peaks"], ("20", "12")):
                item["rule"]["ratchet"] = {"lookback_months": 1, "fraction": "0.5"}
                state["history"] = [{"cycle_end_utc_ms": begin, "peak_kw": peak}]
            report = review(monthly, modeled((begin, feb, "6000"), (feb, march, "2000")), march,
                            archive="concurrent-months.cmr")
            assert "Cycle estimate at closing 68.000000 USD" in report
            assert "Cycle estimate at closing 24.000000 USD" in report
            assert run("--open-review", "concurrent-months.cmr") == report
            monthly["opening"]["demand_peaks"][1]["history"][0]["peak_kw"] = "21"
            review(monthly, modeled((begin, feb, "6000"), (feb, march, "2000")), march, error="saved tariff declaration")

            for change in (
                lambda x: x.update(schema_version=17),
                lambda x: x.update(demand_charge=x["demand_charges"][0]["rule"]),
                lambda x: x["demand_charges"][1].update(key="private-facility"),
                lambda x: x["opening"]["demand_peaks"][1].update(key="private-facility"),
                lambda x: x["opening"]["demand_peaks"].pop(),
                lambda x: x["demand_charges"][1]["rule"].update(eligible_periods=[eligible(16, 45)]),
            ):
                invalid = copy.deepcopy(multiple)
                change(invalid)
                review(invalid, quarterly, close, error="saved tariff declaration")

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
