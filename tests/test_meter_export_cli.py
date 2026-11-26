"""Exercise separate whole-meter export payment and archive binding."""
import copy
from hashlib import sha256
import json
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

from test_meter_bill_named_cli import BUNDLE, HOUR, SCOPE, interval, named, plan, utc

UTC = {"kind": "utc", "fixed_offset_seconds": 0}


def export_tariff(begin, end, clock=UTC, reset="explicit_cycle_only", rates_end=None):
    tariff = plan(begin, begin, end, clock, reset, rates_end)
    tariff["schema_version"] = 12
    tariff["plan"]["export_payment"] = {"rate_per_kwh": "1", "published_utc_ms": begin}
    tariff["plan"]["revisions"][0]["base_rate_per_kwh"] = "3"
    tariff["opening"].update(exported_kwh="0", accrued_export_payment="0")
    return tariff


def scheduled_tariff(begin, end, *rates, **options):
    document = export_tariff(begin, end, **options)
    document["schema_version"] = 14
    document["plan"]["export_payment"] = {"dynamic": [
        {"begin_utc_ms": start, "end_utc_ms": stop, "published_utc_ms": begin, "rate_per_kwh": price}
        for start, stop, price in rates
    ]}
    return document


def net_power(*rows):
    return {"schema_version": 6, "scope": SCOPE, "intervals": [
        {"begin_utc_ms": begin, "end_utc_ms": end, "average_net_watts": watts}
        for begin, end, watts in rows
    ]}


def paired(*rows):
    return {"schema_version": 7, "scope": SCOPE, "register_readings": [
        {"utc_ms": utc_ms, "imported_kwh": imported, "exported_kwh": exported}
        for utc_ms, imported, exported in rows
    ]}


def statement_period(begin, end, **amount):
    return {"begin_utc_ms": begin, "end_utc_ms": end, **amount}


def independent_invoice(supply, exported, export_places=2):
    return {"schema_version": 4, "scope": SCOPE, "currency": "USD",
            "supply": {"decimal_places": 2, "rounding": "half_even", "periods": supply},
            "export": {"decimal_places": export_places, "rounding": "half_away_from_zero", "periods": exported}}


def main(cli, bundle=None):
    with TemporaryDirectory(prefix="canary-export-meter-") as temp:
        root = Path(temp)
        if bundle:
            installed = root / "installed \u03a9"
            installed.mkdir()
            cli = Path(shutil.copy2(cli, installed / cli.name))
            shutil.copytree(bundle, installed / BUNDLE)
        tariff, meter, invoice = [root / name for name in ("private tariff.json", "private meter.json", "private invoice.json")]
        checked = 0

        def write(path, doc):
            path.write_bytes(json.dumps(doc, separators=(",", ":")).encode("ascii"))

        def run(*args, error=None):
            nonlocal checked
            result = subprocess.run([cli, "meter-bill", *map(str, args)], cwd=root,
                                    capture_output=True, text=True, timeout=15)
            assert all(value not in result.stdout + result.stderr
                       for value in (tariff.name, meter.name, invoice.name, SCOPE)), result
            if error:
                assert result.returncode == 3 and not result.stdout and error in result.stderr, result
            else:
                assert result.returncode == 0 and result.stdout and not result.stderr, result
            checked += 1
            return result.stdout

        def review(policy, source, cutoff, *options, error=None):
            write(tariff, policy)
            write(meter, source)
            return run("--tariff", tariff, "--intervals", meter, "--as-of-utc-ms", cutoff, *options, error=error)

        def amounts(text, imported, exported, gross, payment, net):
            assert f"Imported usage {imported} kWh" in text, text
            assert f"Exported usage {exported} kWh" in text, text
            assert f"Cycle estimate at closing {gross} USD" in text, text
            assert f"Export payment to date {payment} USD" in text, text
            assert f"Net electricity cost at closing {net} USD" in text, text

        if bundle:
            begin = utc("2024-11-03T04:00:00")
            end = begin + 4 * HOUR
            signed = net_power((begin, begin + 2 * HOUR, "-2000"), (begin + 2 * HOUR, end, "1000"))
            result = review(export_tariff(begin, end, named("America/New_York")), signed, end,
                            "--save-review", "named.cmr")
            amounts(result, "2.000000", "4.000000", "6.000000", "4.000000", "2.000000")
            assert "Reviewed IANA time database 2026e" in result
            assert run("--open-review", "named.cmr") == result
            # The two local 01:00 hours across the fall-back each have their own UTC rate.
            cut = begin + 2 * HOUR
            policy = scheduled_tariff(begin, end, (begin, cut, "-1"), (cut, end, "2"),
                                      clock=named("America/New_York"))
            result = review(policy, net_power((begin, end, "-1000")), end, "--save-review", "named-scheduled.cmr")
            amounts(result, "0.000000", "4.000000", "0.000000", "2.000000", "-2.000000")
            assert "Reviewed IANA time database 2026e" in result
            assert run("--open-review", "named-scheduled.cmr") == result
            write(invoice, {"schema_version": 3, "scope": SCOPE, "currency": "USD", "decimal_places": 2,
                            "rounding": "half_even", "cycles": [
                                {"begin_utc_ms": begin, "end_utc_ms": end, "supply_charge": "0.00",
                                 "export_payment": "2.00"}
                            ]})
            result = review(policy, net_power((begin, end, "-1000")), end,
                            "--invoice", invoice, "--save-review", "named-invoice.cmr")
            assert "Calculated net electricity cost at invoice precision -2.00 USD" in result
            assert result.count("0.00 USD (match)") == 3
            assert run("--open-review", "named-invoice.cmr") == result
            write(invoice, independent_invoice([statement_period(begin, end, supply_charge="0.00")], [
                statement_period(begin, cut, export_payment="-2.00"),
                statement_period(cut, end, export_payment="4.00"),
            ]))
            result = review(policy, net_power((begin, end, "-1000")), end,
                            "--invoice", invoice, "--save-review", "named-independent.cmr")
            assert "Calculated net electricity cost at invoice precision -2.00 USD" in result
            assert result.count("0.00 USD (match)") == 4
            assert run("--open-review", "named-independent.cmr") == result
        else:
            begin = utc("2024-01-01")
            end, half = begin + HOUR, begin + HOUR // 2
            declaration = export_tariff(begin, end)
            baseline = review(declaration, net_power((begin, end, "-2000")), end, "--save-review", "baseline.cmr")
            amounts(baseline, "0.000000", "2.000000", "0.000000", "2.000000", "-2.000000")
            tariff.unlink()
            meter.unlink()
            assert run("--open-review", "baseline.cmr") == baseline
            amounts(review(declaration, net_power((begin, end, "-1000")), end),
                    "0.000000", "1.000000", "0.000000", "1.000000", "-1.000000")
            amounts(review(declaration, net_power((begin, end, "1000")), end),
                    "1.000000", "0.000000", "3.000000", "0.000000", "3.000000")

            # A cumulative interval can contain import and export at different instants.
            source = paired((begin, "70", "90"), (end, "71", "92"))
            amounts(review(declaration, source, end), "1.000000", "2.000000", "3.000000", "2.000000", "1.000000")
            write(invoice, {"schema_version": 1, "scope": SCOPE, "currency": "USD",
                            "decimal_places": 2, "rounding": "half_even", "cycles": [
                                {"begin_utc_ms": begin, "end_utc_ms": end, "supply_charge": "3.00"}
                            ]})
            result = review(declaration, source, end, "--invoice", invoice)
            assert "Difference (calculated minus invoice) 0.00 USD (match)" in result
            assert "Net electricity cost at closing 1.000000 USD" in result

            missing = "export payment needs signed whole-meter power"
            review(declaration, interval(begin, end), end, error=missing)
            wrong = net_power((begin, end, "-1000"))
            wrong["intervals"][0]["average_watts"] = wrong["intervals"][0].pop("average_net_watts")
            review(declaration, wrong, end, error=missing)
            review(declaration, net_power((begin, end, "-0")), end, error="invalid signed meter power")
            review(declaration, paired((begin, "0", "2"), (end, "1", "1")), end, error="export register decreased")
            review(declaration, paired((begin, "2", "0"), (end, "1", "2")), end, error="meter register decreased")
            wrong_register = copy.deepcopy(source)
            wrong_register["register_readings"][1].pop("exported_kwh")
            review(declaration, wrong_register, end, error="invalid meter register reading")

            old = copy.deepcopy(declaration)
            old.update(schema_version=1)
            old["plan"].pop("export_payment")
            old["opening"].pop("exported_kwh")
            old["opening"].pop("accrued_export_payment")
            review(old, source, end, error="require a saved export-payment tariff")
            old["plan"]["export_payment"] = declaration["plan"]["export_payment"]
            review(old, interval(begin, end), end, error="saved tariff declaration")
            invalid = copy.deepcopy(declaration)
            invalid["opening"]["exported_kwh"] = "1"
            review(invalid, source, end, error="saved tariff declaration")
            invalid = copy.deepcopy(declaration)
            invalid["opening"]["accrued_export_payment"] = "1"
            review(invalid, source, end, error="saved tariff declaration")
            invalid = copy.deepcopy(declaration)
            invalid["plan"]["export_payment"]["published_utc_ms"] = begin + 1
            review(invalid, source, end, error="saved tariff declaration")

            # Cutoff controls publication independently of local confirmation.
            late = copy.deepcopy(declaration)
            late["confirmed_utc"] = (end + 1000) // 1000
            late["plan"]["export_payment"]["published_utc_ms"] = end + 1000
            review(late, source, end, error="rate published after decision instant")
            assert "Net electricity cost at closing 1.000000 USD" in review(late, source, end + 1000)

            # The supply minimum and multiplier apply before the separate payment.
            priced = copy.deepcopy(declaration)
            priced["plan"].update(minimum_cycle_subtotal="5", subtotal_multiplier="2")
            amounts(review(priced, source, end), "1.000000", "2.000000", "10.000000", "2.000000", "8.000000")
            rounded = copy.deepcopy(declaration)
            rounded["plan"]["rounding"] = "meter_import_half_hour_kwh_half_even_2"
            result = review(rounded, paired((begin, "0", "0"), (half, "0.015", "1"), (end, "0.04", "2")), end)
            assert "Billed usage 0.040000 kWh" in result
            amounts(result, "0.040000", "2.000000", "0.120000", "2.000000", "-1.880000")

            demand = copy.deepcopy(declaration)
            demand["demand_charge"] = {"window_minutes": 15, "offset_minutes": 0,
                                       "rate_per_kw": "5", "published_utc_ms": begin}
            demand["opening"]["peak_demand_kw"] = "0"
            result = review(demand, paired((begin, "0", "0"), (begin + HOUR // 4, "0.75", "1"),
                                           (end, "0.75", "3")), end)
            assert "Peak demand 3.000000 kW" in result
            amounts(result, "0.750000", "3.000000", "17.250000", "3.000000", "14.250000")

            # Carry forward previously declared exports and freeze both evidence paths when extending.
            mid = copy.deepcopy(declaration)
            mid["opening"].update(through_utc_ms=half, imported_kwh="1", accrued_subtotal="3",
                                  exported_kwh="2", accrued_export_payment="4")
            report = review(mid, net_power((half, end, "-2000")), end, "--save-review", "mid.cmr")
            amounts(report, "0.000000", "1.000000", "3.000000", "5.000000", "-2.000000")
            assert "Net change after export payment -1.000000 USD" in report
            assert run("--open-review", "mid.cmr") == report
            mid["opening"].pop("accrued_export_payment")
            review(mid, net_power((half, end, "-2000")), end, error="saved tariff declaration")

            initial = paired((begin, "70", "90"), (half, "71", "92"))
            review(declaration, initial, half, "--save-review", "split.cmr")
            later = paired((begin, "70", "90"), (half, "71", "92"), (end, "71", "93"))
            write(meter, later)
            result = run("--extend-review", "split.cmr", "--intervals", meter, "--save-review", "joined.cmr",
                         "--as-of-utc-ms", end)
            amounts(result, "1.000000", "3.000000", "3.000000", "3.000000", "0.000000")
            assert run("--open-review", "joined.cmr") == result
            write(meter, paired((begin, "70", "91"), (half, "71", "93"), (end, "71", "94")))
            run("--extend-review", "split.cmr", "--intervals", meter, "--save-review", "wrong.cmr",
                "--as-of-utc-ms", end, error="differ from the saved review prefix")
            assert not (root / "wrong.cmr").exists()

            feb, mar = utc("2024-02-01"), utc("2024-03-01")
            recurring = export_tariff(begin, feb, reset={"monthly_day": 1, "clock": UTC}, rates_end=mar)
            series = net_power((begin, feb, "-1000"), (feb, mar, "1000"))
            result = review(recurring, series, mar, "--save-review", "recurring.cmr")
            assert "Net electricity cost at closing -744.000000 USD" in result
            assert "Net electricity cost at closing 2088.000000 USD" in result
            assert run("--open-review", "recurring.cmr") == result

            # Scheduled export is independent of the supply rate and multiplier.
            policy = scheduled_tariff(begin, end, (begin, half, "-3"), (half, end, "1"))
            policy["plan"]["subtotal_multiplier"] = "2"
            power = net_power((begin, end, "-2000"))
            result = review(policy, power, end)
            assert "A negative payment increases electricity cost." in result
            amounts(result, "0.000000", "2.000000", "0.000000", "-2.000000", "2.000000")

            split = paired((begin, "0", "10"), (half, "1", "11"), (end, "1", "13"))
            result = review(policy, split, end, "--save-review", "scheduled.cmr")
            amounts(result, "1.000000", "3.000000", "6.000000", "-1.000000", "7.000000")
            assert run("--open-review", "scheduled.cmr") == result
            review(policy, paired((begin, "0", "10"), (end, "1", "13")), end,
                   error="export register needs a reading at an export-rate boundary")
            amounts(review(policy, paired((begin, "0", "10"), (end, "1", "10")), end),
                    "1.000000", "0.000000", "6.000000", "0.000000", "6.000000")
            equal = copy.deepcopy(policy)
            equal["plan"]["export_payment"]["dynamic"][1]["rate_per_kwh"] = "-3"
            amounts(review(equal, paired((begin, "0", "10"), (end, "1", "13")), end),
                    "1.000000", "3.000000", "6.000000", "-9.000000", "15.000000")

            partial = paired((begin, "0", "10"), (half, "1", "11"))
            result = review(policy, partial, half, "--save-review", "scheduled-partial.cmr")
            assert "Export payment to date -3.000000 USD" in result
            write(meter, split)
            result = run("--extend-review", "scheduled-partial.cmr", "--intervals", meter, "--save-review",
                         "scheduled-extended.cmr", "--as-of-utc-ms", end)
            amounts(result, "1.000000", "3.000000", "6.000000", "-1.000000", "7.000000")
            assert run("--open-review", "scheduled-extended.cmr") == result

            # Preserve quarter-hour export evidence and round the summed half-hour imports once.
            quarter = begin + HOUR // 4
            rounded_schedule = scheduled_tariff(begin, half, (begin, quarter, "-2"), (quarter, half, "6"))
            rounded_schedule["plan"]["rounding"] = "meter_import_half_hour_kwh_half_even_2"
            fine = paired((begin, "0", "10"), (quarter, "0.006", "11"), (half, "0.015", "12"))
            review(rounded_schedule, paired((begin, "0", "10"), (half, "0.015", "12")), half,
                   error="export register needs a reading at an export-rate boundary")
            result = review(rounded_schedule, fine, half, "--save-review", "scheduled-rounding.cmr")
            assert "Billed usage 0.020000 kWh" in result
            amounts(result, "0.015000", "2.000000", "0.060000", "4.000000", "-3.940000")
            assert run("--open-review", "scheduled-rounding.cmr") == result

            mid = copy.deepcopy(policy)
            mid["opening"].update(through_utc_ms=half, imported_kwh="1", accrued_subtotal="3",
                                  exported_kwh="1", accrued_export_payment="-3")
            result = review(mid, net_power((half, end, "-4000")), end)
            amounts(result, "0.000000", "2.000000", "6.000000", "-1.000000", "7.000000")
            assert "Net change after export payment -2.000000 USD" in result

            gap = copy.deepcopy(policy)
            gap["plan"]["export_payment"]["dynamic"][1]["begin_utc_ms"] += 1
            review(gap, power, end, error="dynamic price gap")
            review(gap, net_power((begin, end, "0")), end, error="dynamic price gap")
            overlap = copy.deepcopy(policy)
            overlap["plan"]["export_payment"]["dynamic"][1]["begin_utc_ms"] -= 1
            review(overlap, power, end, error="saved tariff declaration")

            late = copy.deepcopy(policy)
            late["confirmed_utc"] = (end + 1000) // 1000
            late["plan"]["export_payment"]["dynamic"][1]["published_utc_ms"] = end + 1000
            review(late, power, end, error="rate published after decision instant")
            amounts(review(late, power, end + 1000),
                    "0.000000", "2.000000", "0.000000", "-2.000000", "2.000000")

            prefix = review(late, partial, half, "--save-review", "before-publication.cmr")
            assert "Export payment to date -3.000000 USD" in prefix
            assert run("--open-review", "before-publication.cmr") == prefix
            write(meter, split)
            run("--extend-review", "before-publication.cmr", "--intervals", meter, "--save-review",
                "before-price.cmr", "--as-of-utc-ms", end, error="rate published after decision instant")
            assert not (root / "before-price.cmr").exists()
            result = run("--extend-review", "before-publication.cmr", "--intervals", meter, "--save-review",
                         "after-price.cmr", "--as-of-utc-ms", end + 1000)
            amounts(result, "1.000000", "3.000000", "6.000000", "-1.000000", "7.000000")
            assert run("--open-review", "after-price.cmr") == result

            recurring = scheduled_tariff(begin, feb, (begin, feb, "-1"), (feb, mar, "1"),
                                         reset={"monthly_day": 1, "clock": UTC}, rates_end=mar)
            result = review(recurring, net_power((begin, mar, "-1000")), mar)
            assert "Export payment to date -744.000000 USD" in result
            assert "Export payment to date 696.000000 USD" in result

            review({**policy, "schema_version": 12}, power, end, error="saved tariff declaration")
            review({**declaration, "schema_version": 14}, power, end, error="saved tariff declaration")
            review({**policy, "plan": {**policy["plan"], "export_payment": {"dynamic": []}}},
                   power, end, error="saved tariff declaration")

            # Round the signed components at invoice precision, then compare the net difference.
            priced = scheduled_tariff(begin, end, (begin, half, "-0.005"), (half, end, "1"))
            priced["plan"]["revisions"][0]["base_rate_per_kwh"] = "0.015"
            billed = paired((begin, "0", "0"), (half, "1", "1"), (end, "1", "1"))
            charge = {"begin_utc_ms": begin, "end_utc_ms": end, "supply_charge": "0.02",
                      "export_payment": "0.00"}
            statement = {"schema_version": 3, "scope": SCOPE, "currency": "USD", "decimal_places": 2,
                         "rounding": "half_even", "cycles": [charge]}
            write(invoice, statement)
            saved = root / "export-invoice.cmr"
            result = review(priced, billed, end, "--invoice", invoice, "--save-review", saved.name)
            assert "Invoice export payment 0.00 USD" in result
            assert "Calculated export payment at invoice precision 0.00 USD" in result
            assert "Calculated net electricity cost at invoice precision 0.02 USD" in result
            assert result.count("0.00 USD (match)") == 3

            before = saved.read_bytes()
            original_invoice = invoice.read_bytes()
            assert before.startswith(b"CANARY-METER-REVIEW-3\n") and original_invoice in before
            for path in (tariff, meter, invoice):
                path.unlink()
            assert run("--open-review", saved.name) == result
            tampered = before[:-65].replace(b'"export_payment":"0.00"', b'"export_payment":"0.01"')
            assert tampered != before[:-65]
            saved.write_bytes(tampered + sha256(tampered).hexdigest().encode("ascii") + b"\n")
            run("--open-review", saved.name, error="differ from the recalculated amounts")
            saved.write_bytes(before)

            away = {**statement, "rounding": "half_away_from_zero",
                    "cycles": [{**charge, "export_payment": "-0.01"}]}
            write(invoice, away)
            result = review(priced, billed, end, "--invoice", invoice)
            assert "Calculated export payment at invoice precision -0.01 USD" in result
            assert "Calculated net electricity cost at invoice precision 0.03 USD" in result
            assert "Net electricity cost at closing 0.020000 USD" in result
            assert result.count("0.00 USD (match)") == 3

            write(invoice, {**statement, "cycles": [{**charge, "supply_charge": "0.01", "export_payment": "-0.01"}]})
            result = review(priced, billed, end, "--invoice", invoice)
            assert "Difference for supply charge (calculated minus invoice) 0.01 USD (mismatch)" in result
            assert "Difference for export payment (calculated minus invoice) 0.01 USD (mismatch)" in result
            assert "Difference for net electricity cost (calculated minus invoice) 0.00 USD (match)" in result

            for value, error in (
                ({**statement, "currency": "GBP"}, "scope or currency differs"),
                ({**statement, "scope": "other"}, "scope or currency differs"),
                ({**statement, "cycles": [{**charge, "export_payment": "-0.005"}]}, "invalid invoice export payment"),
                ({**statement, "cycles": [{**charge, "export_payment": None}]}, "invalid invoice export payment"),
                ({**statement, "cycles": [{**charge, "extra": "0.00"}]}, "invalid invoice export payment"),
                ({**statement, "cycles": [{**charge, "end_utc_ms": half}]}, "cycle bounds differ"),
            ):
                write(invoice, value)
                review(priced, billed, end, "--invoice", invoice, "--save-review", "invalid-invoice.cmr", error=error)
                assert not (root / "invalid-invoice.cmr").exists()

            write(invoice, statement)
            review(priced, paired((begin, "0", "0"), (half, "1", "1")), half, "--invoice", invoice,
                   error="requires complete billing cycles")
            mid_cycle = copy.deepcopy(priced)
            mid_cycle["opening"].update(through_utc_ms=half, imported_kwh="1", accrued_subtotal="0.015",
                                        exported_kwh="1", accrued_export_payment="-0.005")
            review(mid_cycle, paired((half, "1", "1"), (end, "1", "1")), end, "--invoice", invoice,
                   error="from a zero opening")
            unpriced = plan(begin, begin, end, UTC, "explicit_cycle_only")
            unpriced["schema_version"] = 1
            review(unpriced, interval(begin, end), end, "--invoice", invoice,
                   error="requires a declared export payment")

            write(invoice, {**statement, "cycles": [{**charge, "supply_charge": "3.00", "export_payment": "2.00"}]})
            result = review(declaration, source, end, "--invoice", invoice)
            assert "Invoice net electricity cost 1.00 USD" in result
            assert result.count("0.00 USD (match)") == 3
            review(declaration, source, end, "--save-review", "export-meter-only.cmr")
            assert run("--open-review", "export-meter-only.cmr", "--invoice", invoice) == result

            january = {**charge, "end_utc_ms": feb, "supply_charge": "0.00", "export_payment": "-744.00"}
            february = {**january, "begin_utc_ms": feb, "end_utc_ms": mar, "export_payment": "696.00"}
            write(invoice, {**statement, "cycles": [january]})
            january_report = review(recurring, net_power((begin, feb, "-1000")), feb, "--invoice", invoice,
                                    "--save-review", "january-invoice.cmr")
            write(meter, net_power((begin, feb, "-1000"), (feb, mar, "-1000")))
            write(invoice, {**statement, "cycles": [january, february]})
            continuation = run("--extend-review", "january-invoice.cmr", "--intervals", meter, "--invoice", invoice,
                               "--as-of-utc-ms", mar, "--save-review", "two-month-invoice.cmr")
            assert continuation.count("0.00 USD (match)") == 6
            assert "Invoice net electricity cost -696.00 USD" in continuation
            assert run("--open-review", "two-month-invoice.cmr") == continuation
            assert run("--open-review", "january-invoice.cmr") == january_report

            # Sum each rounded export statement. Rounding the unsplit -0.0025 would give -0.003.
            two_hours, three_hours, four_hours = (begin + h * HOUR for h in (2, 3, 4))
            divided = scheduled_tariff(begin, four_hours, (begin, two_hours, "-0.005"),
                                       (two_hours, four_hours, "0.0075"))
            power = net_power((begin, three_hours, "-1000"), (three_hours, four_hours, "1000"))
            supply = statement_period(begin, four_hours, supply_charge="3.00")
            first = statement_period(begin, end, export_payment="-0.005")
            second = statement_period(end, four_hours, export_payment="0.003")
            separated = independent_invoice([supply], [first, second], export_places=3)
            write(invoice, separated)
            result = review(divided, power, four_hours, "--invoice", invoice, "--save-review", "independent.cmr")
            assert "Calculated export payment at invoice precision -0.005 USD" in result
            assert "Calculated export payment at invoice precision 0.003 USD" in result
            assert "Calculated net electricity cost at invoice precision 3.002 USD" in result
            assert result.count("(match)") == 4 and "(mismatch)" not in result
            archive = root / "independent.cmr"
            original = archive.read_bytes()
            assert invoice.read_bytes() in original
            for path in (invoice, tariff, meter):
                path.unlink()
            assert run("--open-review", archive.name) == result
            changed = original[:-65].replace(b'"export_payment":"0.003"', b'"export_payment":"0.004"')
            assert changed != original[:-65]
            archive.write_bytes(changed + sha256(changed).hexdigest().encode("ascii") + b"\n")
            run("--open-review", archive.name, error="differ from the recalculated amounts")
            archive.write_bytes(original)

            for rows in ([first], [first, {**second, "begin_utc_ms": three_hours}]):
                write(invoice, independent_invoice([supply], rows, export_places=3))
                result = review(divided, power, four_hours, "--invoice", invoice)
                assert "Statement net comparison unavailable" in result
                assert "Difference for net electricity cost" not in result
            for doc, error in (
                (independent_invoice([supply], [first, first], export_places=3), "invalid export statement periods"),
                (independent_invoice([supply, supply], []), "invalid supply statement periods"),
                (independent_invoice([], []), "requires at least one statement period"),
                (independent_invoice([{**supply, "end_utc_ms": three_hours}], []), "match a complete reviewed billing cycle"),
                (independent_invoice([], [{**second, "end_utc_ms": four_hours + 1}], export_places=3),
                 "outside the reviewed meter readings"),
                (independent_invoice([], [{**first, "begin_utc_ms": begin - 1}], export_places=3),
                 "outside the reviewed meter readings"),
                ({**separated, "currency": "CAD"}, "scope or currency differs"),
                ({**separated, "export": {**separated["export"], "rounding": "default"}}, "invalid export statement"),
            ):
                write(invoice, doc)
                review(divided, power, four_hours, "--invoice", invoice, "--save-review", "invalid-statement.cmr", error=error)
                assert not (root / "invalid-statement.cmr").exists()

            # The export statement crosses a supply reset and a signed rate change. Only February supply is stated.
            a, b = feb - 12 * HOUR, feb + 6 * HOUR
            offset = independent_invoice([statement_period(feb, mar, supply_charge="0.00")],
                                         [statement_period(a, b, export_payment="-6.00")])
            source = paired((begin, "0", "0"), (a, "0", "732"), (feb, "0", "744"),
                            (b, "0", "750"), (mar, "0", "1440"))
            write(invoice, offset)
            result = review(recurring, source, mar, "--invoice", invoice, "--save-review", "offset-statement.cmr")
            assert "Calculated export payment at invoice precision -6.00 USD" in result
            assert "Exported usage for statement 18.000000 kWh" in result
            assert "Statement net comparison unavailable" in result
            assert result.count("(match)") == 2
            assert run("--open-review", "offset-statement.cmr") == result
            review(recurring, paired((begin, "0", "0"), (feb, "0", "744"), (mar, "0", "1440")), mar,
                   "--invoice", invoice, error="export register needs a reading at a statement boundary")
            review(recurring, source, mar, "--save-review", "offset-meter-only.cmr")
            assert run("--open-review", "offset-meter-only.cmr", "--invoice", invoice) == result

            january_supply = statement_period(begin, feb, supply_charge="0.00")
            write(invoice, independent_invoice([january_supply], [statement_period(a, feb, export_payment="-12.00")]))
            early = paired((begin, "0", "0"), (a, "0", "732"), (feb, "0", "744"))
            previous = review(recurring, early, feb, "--invoice", invoice, "--save-review", "early-statement.cmr")
            write(invoice, offset)
            write(meter, source)
            extended = run("--extend-review", "early-statement.cmr", "--intervals", meter, "--as-of-utc-ms", mar,
                           "--invoice", invoice, "--save-review", "updated-statement.cmr")
            assert "Calculated export payment at invoice precision -6.00 USD" in extended
            assert run("--open-review", "updated-statement.cmr") == extended
            assert run("--open-review", "early-statement.cmr") == previous

            write(invoice, independent_invoice([january_supply, offset["supply"]["periods"][0]], [
                statement_period(begin, b, export_payment="-738.00"),
                statement_period(b, mar, export_payment="690.00"),
            ]))
            multi = review(recurring, source, mar, "--invoice", invoice)
            assert "Calculated net electricity cost at invoice precision 48.00 USD" in multi
            assert multi.count("(match)") == 5

            # An unchanged export register proves zero over every subinterval of the measured span.
            write(invoice, independent_invoice([], [statement_period(half, end, export_payment="0.00")]))
            result = review(divided, paired((begin, "0", "80"), (two_hours, "2", "80")), two_hours, "--invoice", invoice)
            assert "Provisional cycle estimate" in result
            assert "Calculated export payment at invoice precision 0.00 USD" in result
            assert "Exported usage for statement 0.000000 kWh" in result
            assert "Statement net comparison unavailable" in result

            write(invoice, independent_invoice([], [statement_period(half, end, export_payment="1.00")]))
            result = review(declaration, net_power((begin, end, "-2000")), end, "--invoice", invoice)
            assert "Calculated export payment at invoice precision 1.00 USD" in result
            assert "Exported usage for statement 1.000000 kWh" in result
            review(unpriced, interval(begin, end), end, "--invoice", invoice,
                   error="requires a declared export payment")
            write(invoice, independent_invoice([statement_period(begin, end, supply_charge="1.00")], []))
            result = review(unpriced, interval(begin, end), end, "--invoice", invoice)
            assert "Calculated supply charge at invoice precision 1.00 USD" in result
            assert "Statement net comparison unavailable" in result

            # Archive the complete supported statement shape with wide, valid exact amounts.
            months = [utc(f"2024-{month:02d}-01") for month in range(1, 13)] + [utc("2025-01-01")]
            maximum = export_tariff(months[0], months[1], reset={"monthly_day": 1, "clock": UTC}, rates_end=months[-1])
            export_rate = int("9" * 38)
            maximum["plan"]["export_payment"]["rate_per_kwh"] = str(export_rate)
            maximum["plan"]["revisions"][0]["base_rate_per_kwh"] = "8" * 38
            maximum["plan"]["subtotal_multiplier"] = "999999999"
            summed_kwh = 0
            increment = int("6" * 36)
            points, supply_periods, export_periods = [(months[0], "0", "0")], [], []
            for index, (left, right) in enumerate(zip(months, months[1:])):
                supply_periods.append(statement_period(left, right, supply_charge="0"))
                steps = 11 if index < 8 else 10
                for step in range(1, steps + 1):
                    boundary = left + (right - left) * step // steps
                    export_periods.append(statement_period(points[-1][0], boundary, export_payment="0"))
                    summed_kwh += increment
                    points.append((boundary, str(summed_kwh), str(summed_kwh)))
            statement = independent_invoice(supply_periods, export_periods, export_places=6)
            statement["supply"]["decimal_places"] = 6
            write(invoice, statement)
            wide = review(maximum, paired(*points), months[-1], "--invoice", invoice, "--save-review", "wide-statement.cmr")
            assert wide.count("export payment statement [") == 128
            assert f"Calculated export payment at invoice precision {increment * export_rate}.000000 USD" in wide
            assert run("--open-review", "wide-statement.cmr") == wide
            export_periods.append(statement_period(months[-1], months[-1] + HOUR, export_payment="0"))
            write(invoice, statement)
            review(maximum, paired(*points), months[-1], "--invoice", invoice, error="invalid export statement periods")

        print(f"Export payment: {checked} ordinary meter-bill invocations passed")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None)
