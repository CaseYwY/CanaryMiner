"""Exercise separate whole-meter export payment and archive binding."""
import copy
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

        print(f"Export payment: {checked} ordinary meter-bill invocations passed")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None)
