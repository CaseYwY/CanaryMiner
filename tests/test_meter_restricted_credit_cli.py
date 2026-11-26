"""Review restricted opening credits using the ordinary meter and archive commands."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

from test_meter_bill_named_cli import BUNDLE, HOUR, SCOPE, interval, named, plan, register, utc

UTC = {"kind": "utc", "fixed_offset_seconds": 0}


def credit_tariff(begin, through, end, lots, clock=UTC, reset="explicit_cycle_only", rates_end=None):
    tariff = plan(begin, through, end, clock, reset, rates_end)
    tariff["schema_version"] = 13
    tariff["restricted_credit"] = {
        "accrued_import_subtotal": "0",
        "lots": [{"expires_utc_ms": expires, "amount": amount} for expires, amount in lots],
    }
    return tariff


def expect_cycle(text, *, eligible, applied, expired, remaining, cash=None, gross=None):
    for label, value in (("Eligible import energy charges to date", eligible),
                         ("Restricted credit applied", applied),
                         ("Restricted credit expired", expired),
                         ("Restricted credit remaining", remaining),
                         ("Cash due after restricted credit", cash),
                         ("Cycle estimate at closing", gross)):
        if value is None:
            assert label not in text, text
        else:
            assert f"{label} {value} USD" in text, text


def main(cli, bundle=None):
    with TemporaryDirectory(prefix="canary-restricted-meter-") as temp:
        root = Path(temp)
        if bundle:
            installed = root / "installed \u03a9"
            installed.mkdir()
            cli = Path(shutil.copy2(cli, installed / cli.name))
            shutil.copytree(bundle, installed / BUNDLE)

        tariff, meter, invoice = [root / name for name in
                                  ("private tariff.json", "private meter.json", "private invoice.json")]
        checked = 0

        def write(path, doc):
            path.write_text(json.dumps(doc, separators=(",", ":")), encoding="ascii")

        def run(*args, error=None):
            nonlocal checked
            originals = {p: p.read_bytes() for p in (tariff, meter, invoice) if p.exists()}
            result = subprocess.run([cli, "meter-bill", *map(str, args)], cwd=root,
                                    capture_output=True, text=True, timeout=15)
            assert all(value not in result.stdout + result.stderr
                       for value in (tariff.name, meter.name, invoice.name, SCOPE)), result
            assert all(p.read_bytes() == data for p, data in originals.items()), result
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

        year = utc("2023-01-01")
        close = utc("2024-01-01")
        next_close = utc("2025-01-01")
        expired = close + 12 * HOUR
        earlier = close + HOUR
        annual_reset = {"annual_month": 1, "annual_day": 1, "clock": named("Etc/UTC") if bundle else UTC}
        rate_clock = named("America/New_York") if bundle else UTC
        annual = credit_tariff(year, year, close, [(expired, "16"), (next_close + 1, "20")],
                               rate_clock, annual_reset, next_close)
        annual["plan"].update(tiers="progressive", cycle_charge="5")
        annual["plan"]["revisions"][0].update(base_rate_per_kwh="0", bands=[
            {"through_kwh": "10", "rate_per_kwh": "1"}, {"through_kwh": None, "rate_per_kwh": "2"},
        ])
        first = [(year, "80"), (close, "92"), (earlier, "100")]
        full = first + [(expired, "102"), (next_close, "104")]
        old = review(annual, register(first), earlier, "--save-review", "annual-prefix.cmr")
        one, two = old.split("Cycle 2", 1)
        expect_cycle(one, eligible="14.000000", applied="14.000000", expired="0.000000",
                     remaining="22.000000", cash="5.000000", gross="19.000000")
        expect_cycle(two, eligible="8.000000", applied="0.000000", expired="0.000000", remaining="22.000000")
        assert run("--open-review", "annual-prefix.cmr") == old
        write(meter, register(full))
        continued = run("--extend-review", "annual-prefix.cmr", "--intervals", meter, "--as-of-utc-ms", next_close,
                        "--save-review", "continued.cmr")
        assert run("--open-review", "continued.cmr") == continued
        assert "Extends verified meter coverage" in continued
        assert review(annual, register(full), next_close) == "".join(
            line for line in continued.splitlines(keepends=True) if not line.startswith("Extends verified meter coverage")
        )
        one, two = continued.split("Cycle 2", 1)
        assert "Imported usage 12.000000 kWh" in one and "Imported usage 12.000000 kWh" in two, continued
        expect_cycle(two, eligible="14.000000", applied="14.000000", expired="2.000000",
                     remaining="6.000000", cash="5.000000", gross="19.000000")
        # Late observations change neither the earlier applied amount nor the earlier recorded balance.
        assert run("--open-review", "annual-prefix.cmr") == old

        if not bundle:
            begin, end = utc("2024-03-01"), utc("2024-03-01T01:00:00")
            half = begin + HOUR // 2
            rule = credit_tariff(begin, begin, end, [(end + 1, "20")])
            rule["plan"]["cycle_charge"] = "5"
            full_hour = interval(begin, end, "10000")
            report = review(rule, full_hour, end, "--save-review", "closed.cmr")
            expect_cycle(report, eligible="10.000000", applied="10.000000", expired="0.000000",
                         remaining="10.000000", cash="5.000000", gross="15.000000")
            assert run("--open-review", "closed.cmr") == report
            write(invoice, {"schema_version": 1, "scope": SCOPE, "currency": "USD", "decimal_places": 2,
                            "rounding": "half_even", "cycles": [
                                {"begin_utc_ms": begin, "end_utc_ms": end, "supply_charge": "15.00"},
                            ]})
            compared = review(rule, full_hour, end, "--invoice", invoice)
            assert "Difference (calculated minus invoice) 0.00 USD (match)" in compared
            assert "Cash due after restricted credit 5.000000 USD" in compared

            expiry = copy.deepcopy(rule)
            expiry["restricted_credit"]["lots"] = [{"expires_utc_ms": end, "amount": "20"}]
            expect_cycle(review(expiry, full_hour, end), eligible="10.000000", applied="0.000000",
                         expired="20.000000", remaining="0.000000", cash="15.000000", gross="15.000000")
            expiry["restricted_credit"]["lots"][0]["expires_utc_ms"] = half
            partial = review(expiry, interval(begin, half, "10000"), half)
            expect_cycle(partial, eligible="5.000000", applied="0.000000",
                         expired="20.000000", remaining="0.000000")
            # A future review cutoff must not expire a lot past meter coverage or post a provisional cash due.
            early = review(expiry, interval(begin, half - 1, "10000"), end)
            assert "Restricted credit remaining 20.000000 USD" in early
            assert "Cash due after restricted credit" not in early

            opening = copy.deepcopy(rule)
            opening["opening"].update(through_utc_ms=half, imported_kwh="5", accrued_subtotal="10")
            opening["restricted_credit"]["accrued_import_subtotal"] = "5"
            expect_cycle(review(opening, interval(half, end, "10000"), end),
                         eligible="10.000000", applied="10.000000", expired="0.000000",
                         remaining="10.000000", cash="5.000000", gross="15.000000")

            floor = copy.deepcopy(rule)
            floor["plan"].update(minimum_cycle_subtotal="18", subtotal_multiplier="1.1")
            expect_cycle(review(floor, full_hour, end), eligible="11.000000", applied="11.000000",
                         expired="0.000000", remaining="9.000000", cash="8.800000", gross="19.800000")

            demand = copy.deepcopy(rule)
            demand["demand_charge"] = {"window_minutes": 15, "offset_minutes": 0,
                                       "published_utc_ms": begin, "rate_per_kw": "0.5"}
            demand["opening"]["peak_demand_kw"] = "0"
            expect_cycle(review(demand, full_hour, end), eligible="10.000000", applied="10.000000",
                         expired="0.000000", remaining="10.000000", cash="10.000000", gross="20.000000")

            reprice = copy.deepcopy(opening)
            reprice["plan"]["tiers"] = "all_units"
            reprice["plan"]["revisions"][0].update(base_rate_per_kwh="0", bands=[
                {"through_kwh": "5", "rate_per_kwh": "1"}, {"through_kwh": None, "rate_per_kwh": "0.2"},
            ])
            expect_cycle(review(reprice, interval(half, end, "10000"), end),
                         eligible="2.000000", applied="2.000000", expired="0.000000",
                         remaining="18.000000", cash="5.000000", gross="7.000000")

            negative = copy.deepcopy(rule)
            negative["plan"]["revisions"][0]["base_rate_per_kwh"] = "-1"
            expect_cycle(review(negative, full_hour, end), eligible="0.000000", applied="0.000000",
                         expired="0.000000", remaining="20.000000", cash="0.000000", gross="-5.000000")

            flat = "saved tariff declaration"
            for bad_rule in ({"accrued_import_subtotal": "1", "lots": []},
                             {"accrued_import_subtotal": "0", "lots": [{"expires_utc_ms": begin, "amount": "1"}]},
                             {"accrued_import_subtotal": "0", "lots": [{"expires_utc_ms": end, "amount": "-1"}]},
                             {"accrued_import_subtotal": "0", "lots": [{"expires_utc_ms": end, "amount": "0"}]},
                             {"accrued_import_subtotal": "0", "lots": [{"expires_utc_ms": end, "amount": "1"}] * 2},
                             {"accrued_import_subtotal": "0", "lots": [{"expires_utc_ms": end + i, "amount": "1"}
                                                                      for i in range(17)]}):
                invalid = copy.deepcopy(rule)
                invalid["restricted_credit"] = bad_rule
                review(invalid, full_hour, end, error=flat)
            invalid = copy.deepcopy(rule)
            invalid["schema_version"] = 1
            review(invalid, full_hour, end, error=flat)
            invalid = copy.deepcopy(rule)
            invalid.pop("restricted_credit")
            review(invalid, full_hour, end, error=flat)

        print(f"Restricted credit meter review passed {checked} cases")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None)
