"""Exercise public rate capture and offline monetary replay through the installed command."""
import copy
from decimal import Decimal
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from test_meter_tax_cli import fraction, period, tax_tariff
from test_octopus_cycle_review_cli import LONDON, cycle
from test_meter_bill_named_cli import utc
from test_octopus_meter_rates_cli import BEGIN, BUNDLE, END, GO_PRODUCT, HOUR, PAYMENT, PRODUCT, SLOTS, inputs, pages as paginate, row


def main(cli, fixture, bundle, real_worker):
    declaration, pages, readings, _, billed, total = inputs()
    with tempfile.TemporaryDirectory(prefix="canary-capture-rates-") as folder:
        root = Path(folder)
        install = root / "installed"
        install.mkdir()
        shutil.copytree(bundle, install / BUNDLE)
        binary = shutil.copy2(cli, install)
        worker = install / real_worker.name
        shutil.copy2(fixture, worker)

        tariff = root / "tariff notes \u7535.json"
        rate_file = root / "captured.json"
        archive = root / "billing.cmr"
        requests = install / "requests"

        def write(name, value):
            name.write_text(json.dumps(value, separators=(",", ":")), encoding="ascii")

        originals = {}
        for feed, key in ((10, "unit_rate_pages"), (11, "standing_charge_pages")):
            for page, response in enumerate(pages[key], 1):
                path = install / f"{feed}-{page}.json"
                raw = (json.dumps(response, separators=(",", ":")).replace('"value_inc_vat":-12,',
                       '"value_inc_vat":-0.12E2,') + "\n").encode("ascii")
                path.write_bytes(raw)
                originals[path] = raw
        write(tariff, declaration)
        write(root / "meter.json", readings)

        def run(command, *args):
            result = subprocess.run([binary, command, *args], cwd=root, text=True, capture_output=True, timeout=16)
            assert tariff.name not in result.stdout + result.stderr
            assert "FORBIDDEN_DEVICE_SETUP" not in result.stdout + result.stderr
            return result

        args = ("--allow-public-rates", "--tariff", tariff.name, "--payment-method", PAYMENT,
                "--save-rates", rate_file.name)
        hint = run("meter-rates", "--help")
        assert hint.returncode == 0 and "--allow-public-rates" in hint.stdout
        for invalid in (args[1:], ("--allow-public-rate\uff53", *args[1:]),
                        (*args[:-1], "../captured.json"), (*args, "--dns-server", "user.example"),
                        (*args, "--wallet", "forbidden")):
            failed = run("meter-rates", *invalid)
            assert failed.returncode == 2 and not failed.stdout and not requests.exists(), failed
        bad = copy.deepcopy(declaration)
        bad["plan"]["territory"] = "I"
        write(tariff, bad)
        rejected = run("meter-rates", *args)
        assert rejected.returncode == 3 and not requests.exists(), rejected
        write(tariff, declaration)

        positive = run("meter-rates", *args, "--dns-server", "192.168.50.53")
        if sys.platform == "linux" and not requests.exists() and "backend unavailable" in positive.stderr:
            libc = os.confstr("CS_GNU_LIBC_VERSION")
            assert libc and tuple(map(int, libc.split()[1].split(".")[:2])) < (2, 40), positive
            return 77
        assert positive.returncode == 0 and f"Saved {SLOTS} Octopus Agile" in positive.stdout and not positive.stderr, positive
        called = requests.read_text().splitlines()
        expected = [f"{feed} {BEGIN // 1000} {END // 1000} {PRODUCT} C {i} 192.168.50.53"
                    for feed, key in ((10, "unit_rate_pages"), (11, "standing_charge_pages"))
                    for i in range(1, len(pages[key]) + 1)]
        assert called == expected, called
        data = rate_file.read_bytes()
        assert all(raw in data for raw in originals.values())
        published = json.loads(data)
        assert published["product_code"] == PRODUCT and published["payment_method"] == PAYMENT
        reviewed = run("meter-bill", "--tariff", tariff.name, "--intervals", "meter.json",
                       "--octopus-rates", rate_file.name, "--save-review", archive.name)
        assert reviewed.returncode == 0 and f"Cycle estimate at closing {total:.6f} GBP" in reviewed.stdout, reviewed
        replay = run("meter-bill", "--open-review", archive.name)
        assert replay.returncode == 0 and replay.stdout == reviewed.stdout, replay

        exclusive = run("meter-rates", *args)
        assert exclusive.returncode == 3 and rate_file.read_bytes() == data and requests.read_text().splitlines() == called
        rate_file.unlink()
        requests.unlink()
        first = install / "10-1.json"
        inconsistent = copy.deepcopy(pages["unit_rate_pages"][0])
        inconsistent["next"] = None
        write(first, inconsistent)
        failed = run("meter-rates", *args)
        assert failed.returncode == 3 and "incomplete public rate pages" in failed.stderr, failed
        assert len(requests.read_text().splitlines()) == 1 and not rate_file.exists()
        requests.unlink()
        first.write_bytes(originals[first])

        fee = install / "11-1.json"
        invalid_fee = copy.deepcopy(pages["standing_charge_pages"][0])
        invalid_fee["results"][1]["payment_method"] = None
        write(fee, invalid_fee)
        failed = run("meter-rates", *args)
        assert failed.returncode == 3 and "unambiguous London days" in failed.stderr, failed
        assert len(requests.read_text().splitlines()) == len(expected)
        assert not rate_file.exists() and not (root / "captured.json.tmp").exists()

        requests.unlink()
        fee.write_bytes(originals[fee])
        taxed = tax_tariff(copy.deepcopy(declaration),
                          period(BEGIN, END, fraction("vat", "0.05", included=("energy", "daily_fee"))))
        taxed["opening"]["through_utc_ms"] = BEGIN + 24 * HOUR
        write(tariff, taxed)
        opening = run("meter-rates", *args)
        assert opening.returncode == 3 and "captured rates conflict with the tariff opening" in opening.stderr, opening
        assert requests.read_text().splitlines() and not rate_file.exists()
        requests.unlink()

        go = copy.deepcopy(declaration)
        go["plan"]["product"] = GO_PRODUCT
        write(tariff, go)
        write(first, paginate([row(BEGIN - HOUR, END + HOUR, 12.5, PAYMENT),
                               row(BEGIN, END, 99, "NON_DIRECT_DEBIT")])[0])
        fee.write_bytes(originals[fee])
        go_result = run("meter-rates", *args)
        assert go_result.returncode == 0 and "Saved 1 Octopus Go rate intervals" in go_result.stdout, go_result
        assert requests.read_text().splitlines() == [
            f"{feed} {BEGIN // 1000} {END // 1000} {GO_PRODUCT} C 1 -" for feed in (10, 11)]
        go_archive = "go.cmr"
        go_bill = run("meter-bill", "--tariff", tariff.name, "--intervals", "meter.json",
                      "--octopus-rates", rate_file.name, "--save-review", go_archive)
        go_total = (billed * Decimal("12.5") + 26 * Decimal("31.51") + 5 * Decimal("43.125")) / 100
        assert go_bill.returncode == 0 and f"Cycle estimate at closing {go_total:.6f} GBP" in go_bill.stdout, go_bill
        go_replay = run("meter-bill", "--open-review", go_archive)
        assert go_replay.returncode == 0 and go_replay.stdout == go_bill.stdout, go_replay

        # The generated declaration goes through the installed capture command and back into a linked bill review.
        go["reset_policy"] = {"monthly_day": 1, "clock": LONDON}
        write(tariff, go)
        prior = run("meter-bill", "--tariff", tariff.name, "--intervals", "meter.json",
                    "--octopus-rates", rate_file.name, "--save-review", "march-monthly.cmr")
        assert prior.returncode == 0, prior
        generated = run("meter-bill", "--prepare-next-cycle", "march-monthly.cmr", "--save-tariff", "april.json")
        assert generated.returncode == 0 and "Payment method DIRECT_DEBIT" in generated.stdout, generated
        ends = utc("2023-04-30T23:00:00")
        register_end = Decimal(readings["register_readings"][-1]["imported_kwh"])
        _, new_register, new_snapshot, new_total = cycle(END, ends, GO_PRODUCT, register_end, 25, 40, 30)
        write(first, new_snapshot["unit_rate_pages"][0])
        write(fee, new_snapshot["standing_charge_pages"][0])
        write(root / "april-meter.json", new_register)
        requests.unlink()
        captured = run("meter-rates", "--allow-public-rates", "--tariff", "april.json", "--payment-method",
                       PAYMENT, "--save-rates", "april-rates.json")
        assert captured.returncode == 0 and "Saved 1 Octopus Go rate intervals" in captured.stdout, captured
        assert requests.read_text().splitlines() == [
            f"{feed} {END // 1000} {ends // 1000} {GO_PRODUCT} C 1 -" for feed in (10, 11)]
        applied = run("meter-bill", "--next-cycle-review", "march-monthly.cmr", "--tariff", "april.json",
                      "--intervals", "april-meter.json", "--octopus-rates", "april-rates.json",
                      "--save-review", "april.cmr")
        assert applied.returncode == 0 and f"Cycle estimate at closing {new_total:.6f} GBP" in applied.stdout, applied
        replay = run("meter-bill", "--open-review", "april.cmr", "--previous-cycle", "march-monthly.cmr")
        assert replay.returncode == 0 and replay.stdout == applied.stdout, replay

        base = ("10", str(BEGIN // 1000), str(END // 1000), "5000", "15000", "65536", "32768")
        for malformed in (("GO-VAR-22-10-14", "I", "1"), ("AGILE-23-02-29", "C", "1"),
                          (PRODUCT, "C", "83"), (PRODUCT, "C", "01")):
            result = subprocess.run([real_worker, *base, *malformed], capture_output=True, timeout=4)
            assert result.returncode == 2 and not result.stdout and not result.stderr, result

        print("Octopus installed capture: 1,486 Agile half hours, clipped Go prices, exact billing and offline replay")
    return 0


if __name__ == "__main__":
    sys.exit(main(*(Path(arg).resolve() for arg in sys.argv[1:])))
