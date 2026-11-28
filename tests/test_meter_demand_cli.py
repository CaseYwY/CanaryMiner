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

            start = utc("2024-11-01T03:00:00")
            month_close, finish = start + HOUR, start + 2 * HOUR
            independent = declaration(start, finish, minutes=60)
            independent.update(schema_version=20, demand_reset_policy=dict(monthly_day=1, clock=named("America/New_York")))
            independent["opening"].update(peak_demand_kw="3", demand_billing=dict(
                imported_kwh="3", posted_subtotal="0", time_database_version="2026e"))
            readings = register([(start, "0"), (month_close, "0"), (finish, "2")])
            report = review(independent, readings, finish, archive="independent-named.cmr")
            assert "Reviewed IANA time database 2026e" in report
            assert f"Open demand month [{month_close}, " in report
            assert "Open-month peak demand 2.000000 kW" in report
            assert "Cycle estimate at closing 17.000000 USD" in report
            assert run("--open-review", "independent-named.cmr") == report
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

            # Contracted capacity charges once per cycle. Only peak demand above it uses the excess rate.
            contracted = declaration(begin, rounded_end, minutes=30)
            contracted["schema_version"] = 19
            contracted["demand_charge"].update(rate_per_kw="4", contracted_capacity=dict(capacity="10", rate_per_unit="2"))
            steps = [(begin, "0"), (halfway, "4"), (rounded_end, "10")]
            prefix = review(contracted, register(steps[:2]), halfway, archive="capacity-prefix.cmr")
            assert "Provisional cycle estimate 24.000000 USD" in prefix
            assert "Excess demand fee 0.000000 USD" in prefix
            prefix_bytes = (root / "capacity-prefix.cmr").read_bytes()
            write(meter, register(steps))
            extended = run("--extend-review", "capacity-prefix.cmr", "--intervals", meter,
                           "--as-of-utc-ms", rounded_end, "--save-review", "capacity-final.cmr")
            totals(extended, "12.000000", "38.000000")
            assert "Contracted capacity 10.000000 kW" in extended
            assert "Contracted capacity fee 20.000000 USD" in extended and "Excess demand fee 8.000000 USD" in extended
            assert run("--open-review", "capacity-final.cmr") == extended
            assert (root / "capacity-prefix.cmr").read_bytes() == prefix_bytes
            totals(review(contracted, register([(begin, "0"), (rounded_end, "0")]), rounded_end),
                   "0.000000", "20.000000")
            review({**contracted, "schema_version": 9}, register(steps), rounded_end, error="saved tariff declaration")

            taxed_capacity = copy.deepcopy(contracted)
            taxed_capacity["opening"].update(through_utc_ms=halfway, imported_kwh="4",
                                              accrued_subtotal="4", peak_demand_kw="8",
                                              tax_bases=dict(energy="4", daily_fee="0", cycle_fee="0",
                                                             demand="20", billed_kwh="0"))
            taxed_capacity["plan"]["tax_periods"] = [dict(
                begin_utc_ms=begin, end_utc_ms=rounded_end, published_utc_ms=begin,
                rules=[dict(key="demand-tax", kind="fraction", rate="0.1", components=["demand"],
                            included_in=[], prior_taxes=[])])]
            totals(review(taxed_capacity, register(steps[1:]), rounded_end), "12.000000", "40.800000")
            taxed_capacity["opening"]["tax_bases"]["demand"] = "0"
            review(taxed_capacity, register(steps[1:]), rounded_end, error="saved tariff declaration")

            mixed_capacity = concurrent(contracted, dict(window_minutes=30, offset_minutes=0,
                                                        published_utc_ms=begin, rate_per_kw="1"))
            mixed_capacity["schema_version"] = 19
            totals(review(mixed_capacity, register(steps), rounded_end), "12.000000", "50.000000")
            apparent_capacity = copy.deepcopy(contracted)
            rule = apparent_capacity["demand_charge"]
            rule["rate_per_kva"] = rule.pop("rate_per_kw")
            rule["power_factor_va_places"] = None
            apparent_capacity["opening"]["peak_demand_kva"] = apparent_capacity["opening"].pop("peak_demand_kw")
            paired = modeled((begin, halfway, "8000"), (halfway, rounded_end, "12000"))
            paired["schema_version"] = 4
            for row, va in zip(paired["intervals"], ("11000", "15000")):
                row["average_va"] = va
            report = review(apparent_capacity, paired, rounded_end)
            assert "Contracted capacity 10.000000 kVA" in report and "Cycle estimate at closing 50.000000 USD" in report

            jan, feb, mar = (utc(f"2024-{month}-01T00:00:00") for month in ("01", "02", "03"))
            recurring_capacity = declaration(jan, feb, reset=dict(monthly_day=1, clock=UTC), rates_end=mar, minutes=30)
            recurring_capacity["schema_version"] = 19
            recurring_capacity["demand_charge"]["contracted_capacity"] = dict(capacity="10", rate_per_unit="2")
            report = review(recurring_capacity, register([(jan, "0"), (feb, "0"), (mar, "0")]), mar)
            assert report.count("Cycle estimate at closing 20.000000 USD") == 2, report
            history_capacity = declaration(feb, mar, reset=dict(monthly_day=1, clock=UTC), minutes=30)
            history_capacity["schema_version"] = 19
            history_capacity["demand_charge"].update(rate_per_kw="4",
                contracted_capacity=dict(capacity="10", rate_per_unit="2"),
                ratchet=dict(lookback_months=1, fraction="0.75"))
            history_capacity["opening"]["demand_history"] = [dict(cycle_end_utc_ms=feb, peak_kw="20")]
            report = review(history_capacity, register([(feb, "0"), (mar, "0")]), mar)
            totals(report, "0.000000", "40.000000")
            assert "Billable demand 15.000000 kW" in report and "Excess demand fee 20.000000 USD" in report

            jan15, feb15, mar15 = (utc(f"2024-{month}-15T00:00:00") for month in ("01", "02", "03"))
            jan_close, feb_close = (utc(day) for day in ("2024-01-31T23:00:00", "2024-02-29T23:00:00"))
            independent = declaration(jan15, feb15, reset=dict(monthly_day=15, clock=UTC), rates_end=mar15, minutes=30)
            independent.update(schema_version=20, demand_reset_policy=dict(
                monthly_day=1, clock=dict(kind="fixed_standard", fixed_offset_seconds=3600)))
            independent["demand_charge"].update(rate_per_kw="4",
                contracted_capacity=dict(capacity="10", rate_per_unit="2"))
            independent["opening"].update(peak_demand_kw="12", demand_billing=dict(
                imported_kwh="6", posted_subtotal="0", time_database_version="explicit-fixed-clock"))
            before_close = jan_close - HOUR
            monthly_steps = [(jan15, "0"), (before_close, "0"), (jan_close, "0"), (feb15 - HOUR // 2, "0"),
                             (feb15, "8"), (feb_close, "8"), (mar15, "8")]
            report = review(independent, register(monthly_steps[:2]), before_close)
            assert "Posted demand fees in this supply cycle 0.000000 USD" in report
            assert "Open-month peak demand 12.000000 kW" in report
            partial = review(independent, register(monthly_steps[:3]), jan_close, archive="independent-prefix.cmr")
            assert "Provisional cycle estimate 28.000000 USD" in partial
            assert "Open-month peak demand 0.000000 kW" in partial
            assert run("--open-review", "independent-prefix.cmr") == partial
            original = (root / "independent-prefix.cmr").read_bytes()
            report = review(independent, register(monthly_steps), mar15)
            assert "Posted demand fees in this supply cycle 28.000000 USD" in report
            assert "Cycle estimate at closing 36.000000 USD" in report
            assert "Open-month peak demand 16.000000 kW" in report
            assert "Posted demand fees in this supply cycle 44.000000 USD" in report
            assert "Cycle estimate at closing 44.000000 USD" in report
            extended = run("--extend-review", "independent-prefix.cmr", "--intervals", meter,
                           "--as-of-utc-ms", mar15, "--save-review", "independent-extended.cmr")
            predecessor = f"Extends verified meter coverage through {jan_close} UTC ms "
            assert sum(line.startswith(predecessor) for line in extended.splitlines()) == 1
            assert [line for line in extended.splitlines() if not line.startswith(predecessor)] == report.splitlines()
            assert run("--open-review", "independent-extended.cmr") == extended
            assert (root / "independent-prefix.cmr").read_bytes() == original
            review(independent, register([(jan15, "0"), (feb15, "8")]), feb15, error=boundary)
            invalid = copy.deepcopy(independent)
            invalid["opening"]["demand_billing"]["posted_subtotal"] = "1"
            review(invalid, register(monthly_steps), mar15, error="saved tariff declaration")
            invalid["opening"]["demand_billing"]["posted_subtotal"] = "0"
            invalid["demand_reset_policy"]["clock"]["fixed_offset_seconds"] = 900
            review(invalid, register(monthly_steps), mar15, error="saved tariff declaration")

            resumed = copy.deepcopy(independent)
            resumed["opening"].update(through_utc_ms=jan_close, peak_demand_kw="0",
                                      demand_billing=dict(imported_kwh="0", posted_subtotal="28",
                                                          time_database_version="explicit-fixed-clock"),
                                      tax_bases=dict(energy="0", daily_fee="0", cycle_fee="0", demand="28", billed_kwh="0"))
            resumed["plan"]["tax_periods"] = [dict(begin_utc_ms=jan15, end_utc_ms=feb15,
                published_utc_ms=jan15, rules=[dict(key="demand-tax", kind="fraction", rate="0.1",
                components=["demand"], included_in=[], prior_taxes=[])])]
            report = review(resumed, register(monthly_steps[2:5]), feb15)
            assert "Cycle estimate at closing 38.800000 USD" in report
            resumed["opening"]["tax_bases"]["demand"] = "0"
            review(resumed, register(monthly_steps[2:5]), feb15, error="saved tariff declaration")

            demand_history = copy.deepcopy(independent)
            annual_end = utc("2025-01-15")
            demand_history["reset_policy"] = dict(annual_month=1, annual_day=15, clock=UTC)
            demand_history["opening"]["cycle_end_utc_ms"] = annual_end
            demand_history["plan"]["revisions"][0]["end_utc_ms"] = annual_end
            demand_history["demand_charge"]["ratchet"] = dict(lookback_months=1, fraction="0.75")
            dec_close = utc("2023-12-31T23:00:00")
            demand_history["opening"]["demand_history"] = [dict(cycle_end_utc_ms=dec_close, peak_kw="20")]
            report = review(demand_history, register(monthly_steps[:3]), jan_close)
            assert "Posted demand fees in this supply cycle 40.000000 USD" in report
            assert "Demand ratchet floor 9.000000 kW" in report

            paired_independent = concurrent(independent, dict(window_minutes=30, offset_minutes=0,
                published_utc_ms=jan15, rate_per_kva="3", power_factor_va_places=None))
            paired_independent["schema_version"] = 20
            paired_independent["opening"]["demand_peaks"][1]["peak"] = "16"
            apparent_month = modeled((jan15, jan_close, "0"))
            apparent_month["schema_version"] = 4
            apparent_month["intervals"][0]["average_va"] = "0"
            report = review(paired_independent, apparent_month, jan_close)
            assert "Posted demand fees in this supply cycle 76.000000 USD" in report

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
            disjoint = copy.deepcopy(existing)
            disjoint["demand_charges"][0]["rule"].update(window_minutes=15, eligible_periods=[eligible(0, 15)])
            disjoint["demand_charges"][1]["rule"]["eligible_periods"] = [eligible(15, 30)]
            disjoint["opening"].update(imported_kwh="3", accrued_subtotal="3")
            for state in disjoint["opening"]["demand_peaks"]:
                state["peak"] = "8"
            review(disjoint, register(register_points[2:]), close, error="saved tariff declaration")
            disjoint["opening"].update(imported_kwh="4", accrued_subtotal="4")
            report = review(disjoint, register(register_points[2:]), close)
            totals(report, "8.000000", "71.000000")
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
