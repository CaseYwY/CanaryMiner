"""Exercise the ordinary offline command with the actual miner's host CLI."""
from concurrent.futures import ThreadPoolExecutor
import copy
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import time
import unittest


MINER = Path(sys.argv.pop(1)).resolve() if __name__ == "__main__" else None
END = 1760000040
START = END - 3600


def sample():
    def series(identity, early, late, offset):
        return {
            "series": identity,
            "observed_utc": END + offset,
            "minutes": [
                {"start_utc": START + i * 60, "closed": True, "close": early if i < 30 else late}
                for i in range(60)
            ],
        }

    return {
        "schema_version": 1,
        "end_utc": END,
        "pearl": series("gate-pearl-mainnet-usdt-v1", "2.25", "4.5", 1),
        "fx": series("kraken-usdt-usd-v1", "0.5", "1.5", 2),
    }


def vendor_sample(pearl_begin=-1, fx_begin=-1):
    def mexc(i):
        at = (START + i * 60) * 1000
        price = "2.250" if i < 30 else "4.50"
        return [at, price, price, price, price, "1.00", at + 60000, price]

    def coinbase(i):
        price = "0.500" if i < 30 else "1.50"
        return {"start": str(START + i * 60), "open": price, "high": price,
                "low": price, "close": price, "volume": "1.00"}

    # A response can be newest first and include a prior minute and the open terminal row.
    return {
        "schema_version": 2,
        "end_utc": END,
        "pearl": {"series": "mexc-pearl-mainnet-usdt-v1", "observed_utc": END + 1,
                  "candles": [mexc(i) for i in reversed(range(pearl_begin, 61))]},
        "fx": {"series": "coinbase-usdt-usd-v1", "observed_utc": END + 2,
               "candles": {"candles": [coinbase(i) for i in reversed(range(fx_begin, 61))]}},
    }


def fallback_sample():
    def kraken(i):
        price = "1.25" if i < 30 else "2" if i < 60 else "99"
        return [START + i * 60, price, price, price, price, price, "1", 1]

    doc = vendor_sample(fx_begin=1)
    doc["schema_version"] = 3
    doc["kraken_fallback"] = {
        "series": "kraken-usdt-usd-v1",
        "observed_utc": END + 7,
        "candles": {"error": [], "result": {"USDTZUSD": [kraken(i) for i in range(-1, 61)],
                                           "last": END - 60}},
    }
    return doc


@unittest.skipUnless(MINER is not None, "run directly with the built miner CLI")
class MarketHourCli(unittest.TestCase):
    def setUp(self):
        stage = tempfile.TemporaryDirectory(prefix="canary-hour-")
        self.addCleanup(stage.cleanup)
        self.stage = Path(stage.name)
        self.file = self.stage / "private-\u884c\u60c5-\u20ac.json"
        # Check that neighbouring local data stays untouched during review.
        (self.stage / "config.json").write_text("CONFIG_SENTINEL", encoding="utf-8")

    def run_miner(self, *args):
        result = subprocess.run([str(MINER), *args], cwd=self.stage, capture_output=True, text=True, timeout=5)
        for sentinel in ("CONFIG_SENTINEL", "FORBIDDEN_DEVICE_SETUP", "private-"):
            self.assertNotIn(sentinel, result.stdout + result.stderr)
        return result

    def run_review(self, doc):
        self.file.write_text(json.dumps(doc), encoding="utf-8")
        return self.run_miner("market-hour", "--file", self.file.name)

    def assert_refusal(self, result, fragment="", code=3):
        self.assertEqual(result.returncode, code, result)
        self.assertEqual(result.stdout, "")
        self.assertTrue(result.stderr.startswith("market-hour: "), result)
        self.assertIn(fragment, result.stderr)

    def test_help_and_strict_options(self):
        for option in ("--help", "-h"):
            result = self.run_miner("market-hour", option)
            self.assertEqual((result.returncode, result.stderr), (0, ""))
            self.assertIn("--file FILE", result.stdout)
        self.assertIn("market-hour", self.run_miner("--help").stdout)
        for args in ((), ("--file",), ("--file", ""), ("--help", "--file", self.file.name),
                     ("--config", self.file.name), ("--file", self.file.name, "--pool", "fixture")):
            with self.subTest(args=args):
                self.assert_refusal(self.run_miner("market-hour", *args), code=2)

    def test_open_a_partial_public_sample_offline(self):
        cutoff = END + 5
        payload = json.dumps(vendor_sample()["pearl"]["candles"]).encode("ascii")
        pearl = f"CMJSON1 0 200 {cutoff} 0 {len(payload)}\n".encode("ascii") + payload
        limited = b"CMJSON1 10 429 0 1 0\n"

        def receipt(wire, received=cutoff):
            return f"{cutoff}\n{received}\n{len(wire)}\n".encode("ascii") + wire

        report = (
            "One public candle sample; candle finality is unverified.\n"
            f"UTC seconds [{START}, {END}); window ended 5 s ago\n"
            f"MEXC Pearl mainnet PRL/USDT: 60/60 elapsed minute buckets; observed UTC {cutoff} (receipt age 0 s)\n"
            "Coinbase USDT/USD: provider rate limited the request\n"
            "Mean USDT per PRL (exact sum / minutes): 202.5 / 60\n"
            "Mean USD per PRL (exact sum / minutes): N/A (requires complete aligned PRL and FX coverage)\n"
            "Sample result: incomplete\n"
            "Coinbase USDT/USD HTTP status 429 (Retry-After 1 s)\n"
            f"Capture UTC {cutoff} (ages at capture)\n"
        ).encode("ascii")
        header = f"CANARY-MARKET-HOUR-REVIEW-4\n{cutoff}\n{END}\n1\n2\n".encode("ascii")
        path = self.stage / "partial.cmh"

        def reopen(second_receipt):
            body = header + receipt(pearl) + second_receipt + str(len(report)).encode("ascii") + b"\n" + report
            path.write_bytes(body + hashlib.sha256(body).hexdigest().encode("ascii") + b"\n")
            return self.run_miner("market-hour", "--open-review", path.name)

        replayed = reopen(receipt(limited))
        self.assertEqual((replayed.returncode, replayed.stderr, replayed.stdout), (0, "", report.decode("ascii")))
        self.assert_refusal(reopen(receipt(limited, received=cutoff - 1)), "archive unavailable or invalid")
        self.assert_refusal(reopen(receipt(b"CMJSON1 4 0 0 0 0\n")), "archive unavailable or invalid")
        self.assert_refusal(reopen(receipt(b"CMJSON1 14 0 0 0 0\n")), "archive unavailable or invalid")

    def test_local_file_and_exact_same_minute_conversion(self):
        result = self.run_review(sample())
        self.assertEqual((result.returncode, result.stderr), (0, ""), result)
        self.assertIn("offline; freshness unverified", result.stdout)
        self.assertIn(f"UTC seconds [{START}, {END})", result.stdout)
        self.assertIn(f"60/60 closed minutes; declared observation UTC {END + 1}", result.stdout)
        self.assertIn(f"60/60 closed minutes; declared observation UTC {END + 2}", result.stdout)
        self.assertRegex(result.stdout, r"\(age [0-9]+ s\)")
        self.assertIn("Mean USDT per PRL (exact sum / minutes): 202.5 / 60", result.stdout)
        self.assertIn("Mean USD per PRL (exact sum / minutes): 236.25 / 60", result.stdout)
        self.assertEqual({p.name for p in self.stage.iterdir()}, {"config.json", self.file.name})
        self.assertEqual((self.stage / "config.json").read_text(), "CONFIG_SENTINEL")
        self.assertEqual(json.loads(self.file.read_text()), sample())

    def test_vendor_payloads_normalize_the_same_hour(self):
        doc = vendor_sample()
        result = self.run_review(doc)
        self.assertEqual((result.returncode, result.stderr), (0, ""), result)
        self.assertIn("offline; freshness unverified", result.stdout)
        self.assertIn("Candle finality is unverified", result.stdout)
        self.assertIn("Declared MEXC Pearl mainnet PRL/USDT: 60/60 elapsed minute buckets", result.stdout)
        self.assertIn("Declared Coinbase USDT/USD: 60/60 elapsed minute buckets", result.stdout)
        self.assertNotIn("closed minutes", result.stdout)
        self.assertIn("202.5 / 60", result.stdout)
        self.assertIn("236.25 / 60", result.stdout)
        self.assertEqual(json.loads(self.file.read_text(encoding="utf-8")), doc)
        self.assertEqual((self.stage / "config.json").read_text(), "CONFIG_SENTINEL")
        self.assertEqual({p.name for p in self.stage.iterdir()}, {"config.json", self.file.name})

        for missing in (1, 30, 60):
            doc = vendor_sample()
            del doc["pearl"]["candles"][missing]
            self.assert_refusal(self.run_review(doc), "requires all 60 elapsed minute buckets")
        for count in (0, 59):
            doc = vendor_sample()
            rows = doc["fx"]["candles"]["candles"]
            doc["fx"]["candles"]["candles"] = rows[1:count+1]
            result = self.run_review(doc)
            self.assertEqual((result.returncode, result.stderr), (0, ""), result)
            self.assertIn(f"{count}/60 elapsed minute buckets", result.stdout)
            self.assertIn("incomplete aligned FX coverage", result.stdout)
            self.assertNotIn("Mean USD per PRL (exact", result.stdout)
        doc = vendor_sample()
        del doc["fx"]
        self.assertIn("unavailable (no FX series)", self.run_review(doc).stdout)

    def test_vendor_payloads_reject_wrong_provenance_and_malformed_candles(self):
        for label, mutate in (
            ("foreign symbol", lambda d: d["pearl"].update(series="private-foreign")),
            ("v1 series in v2", lambda d: d.update(pearl=sample()["pearl"])),
            ("wrong Coinbase shape", lambda d: d["fx"].update(candles=[])),
            ("foreign field", lambda d: d["fx"]["candles"].update(product="private-foreign")),
            ("premature observation", lambda d: d["pearl"].update(observed_utc=END-1)),
            ("future observation", lambda d: d["fx"].update(observed_utc=int(time.time())+3600)),
            ("duplicate outside hour", lambda d: d["pearl"]["candles"].append(d["pearl"]["candles"][0])),
            ("duplicate FX", lambda d: d["fx"]["candles"]["candles"].append(d["fx"]["candles"]["candles"][1])),
            ("MEXC wrong units", lambda d: d["pearl"]["candles"][1].__setitem__(0, END-60)),
            ("MEXC unexpected close convention", lambda d: d["pearl"]["candles"][1].__setitem__(6, END*1000-1)),
            ("MEXC excessive timestamp", lambda d: d["pearl"]["candles"][1].__setitem__(0, 2**64-1)),
            ("MEXC unavailable member", lambda d: d["pearl"]["candles"][1].pop()),
            ("MEXC impossible high", lambda d: d["pearl"]["candles"][1].__setitem__(2, "0.1")),
            ("invalid quote volume", lambda d: d["pearl"]["candles"][1].__setitem__(7, "-10")),
            ("large price", lambda d: d["pearl"]["candles"][1].__setitem__(4, "9"*39)),
            ("Coinbase milliseconds", lambda d: d["fx"]["candles"]["candles"][1].update(start=str((END-60)*1000))),
            ("Coinbase future row", lambda d: d["fx"]["candles"]["candles"][0].update(start=str(END+60))),
            ("Coinbase start must be string", lambda d: d["fx"]["candles"]["candles"][1].update(start=END-60)),
            ("Coinbase non-minute start", lambda d: d["fx"]["candles"]["candles"][1].update(start=str(END-61))),
            ("Coinbase precision out of range", lambda d: d["fx"]["candles"]["candles"][1].update(close="0.0000000000000000001")),
            ("Coinbase exponent", lambda d: d["fx"]["candles"]["candles"][1].update(close="1e0")),
            ("Coinbase unknown field", lambda d: d["fx"]["candles"]["candles"][1].update(closed=True)),
        ):
            with self.subTest(case=label):
                doc = vendor_sample()
                mutate(doc)
                self.assert_refusal(self.run_review(doc), "invalid")

    def test_vendor_row_bounds_use_distinct_plausible_minutes(self):
        for key, lower_bound in (("pearl_begin", -439), ("fx_begin", -289)):
            with self.subTest(key=key):
                result = self.run_review(vendor_sample(**{key: lower_bound}))
                self.assertEqual((result.returncode, result.stderr), (0, ""), result)
                self.assertIn("236.25 / 60", result.stdout)
                self.assert_refusal(self.run_review(vendor_sample(**{key: lower_bound-1})), "invalid")

    def test_vendor_hour_uses_a_complete_independent_fallback(self):
        doc = fallback_sample()
        result = self.run_review(doc)
        self.assertEqual((result.returncode, result.stderr), (0, ""), result)
        self.assertIn("offline; freshness unverified", result.stdout)
        self.assertIn("Declared Coinbase USDT/USD: 59/60 elapsed minute buckets", result.stdout)
        self.assertIn("Declared Kraken USDT/USD fallback: 60/60 elapsed minute buckets", result.stdout)
        self.assertIn("minute starts inferred from declared observation", result.stdout)
        self.assertIn("The USD mean uses the declared Kraken series.", result.stdout)
        self.assertIn("Mean USDT per PRL (exact sum / minutes): 202.5 / 60", result.stdout)
        self.assertIn("Mean USD per PRL (exact sum / minutes): 354.375 / 60", result.stdout)

        rows = doc["kraken_fallback"]["candles"]["result"]["USDTZUSD"]
        del rows[18]  # Coinbase covers this minute, but the two incomplete FX series cannot be merged.
        result = self.run_review(doc)
        self.assertEqual((result.returncode, result.stderr), (0, ""), result)
        self.assertIn("Declared Kraken USDT/USD fallback: 59/60 elapsed minute buckets", result.stdout)
        self.assertIn("Mean USDT per PRL (exact sum / minutes): 202.5 / 60", result.stdout)
        self.assertIn("incomplete aligned FX coverage", result.stdout)
        self.assertNotIn("Mean USD per PRL (exact", result.stdout)

    def test_vendor_fallback_requires_a_valid_primary_gap_and_kraken_payload(self):
        for label, mutate, expected in (
            ("no Coinbase declaration", lambda d: d.pop("fx"), "invalid hour document"),
            ("no Kraken declaration", lambda d: d.pop("kraken_fallback"), "invalid hour document"),
            ("old schema with extra source", lambda d: d.update(schema_version=2), "invalid hour document"),
            ("complete Coinbase", lambda d: d.update(fx=vendor_sample()["fx"]), "incomplete Coinbase"),
            ("malformed Coinbase", lambda d: d["fx"].update(candles=[]), "invalid USDT/USD minute series"),
            ("wrong Kraken series", lambda d: d["kraken_fallback"].update(series="private-foreign"), "invalid Kraken"),
            ("terminal row missing", lambda d: d["kraken_fallback"]["candles"]["result"]["USDTZUSD"].pop(), "invalid Kraken"),
            ("observed after current row", lambda d: d["kraken_fallback"].update(observed_utc=END + 60), "invalid Kraken"),
            ("incomplete Pearl", lambda d: d["pearl"]["candles"].pop(1), "requires all 60"),
        ):
            with self.subTest(case=label):
                doc = fallback_sample()
                mutate(doc)
                self.assert_refusal(self.run_review(doc), expected)

        doc = fallback_sample()
        pairs = doc["kraken_fallback"]["candles"]["result"]
        pairs["OTHER"] = pairs.pop("USDTZUSD")
        self.assert_refusal(self.run_review(doc), "invalid Kraken")

    def test_vendor_fallback_saves_and_replays_with_the_declared_original(self):
        original = json.dumps(fallback_sample(), separators=(",", ":")).encode()
        self.file.write_bytes(original)
        archive = self.stage / "fallback.cmh"
        result = self.run_miner("market-hour", "--file", self.file.name, "--save-review", archive.name)
        self.assertEqual((result.returncode, result.stderr), (0, ""), result)
        self.assertIn("354.375 / 60", result.stdout)
        self.assertIn(original, archive.read_bytes())
        self.file.unlink()
        replay = self.run_miner("market-hour", "--open-review", archive.name)
        self.assertEqual((replay.returncode, replay.stderr, replay.stdout), (0, "", result.stdout))

    def test_missing_or_partial_fx(self):
        doc = sample()
        del doc["fx"]["minutes"][20]
        result = self.run_review(doc)
        self.assertEqual((result.returncode, result.stderr), (0, ""), result)
        self.assertIn("59/60 closed minutes", result.stdout)
        self.assertIn("unavailable (incomplete aligned FX coverage)", result.stdout)
        self.assertIn("202.5 / 60", result.stdout)
        self.assertNotIn("Mean USD per PRL (exact", result.stdout)
        del doc["fx"]
        result = self.run_review(doc)
        self.assertEqual((result.returncode, result.stderr), (0, ""), result)
        self.assertIn("unavailable (no FX series)", result.stdout)

    def test_fx_arithmetic_keeps_the_prl_mean(self):
        doc = sample()
        for name in ("pearl", "fx"):
            for row in doc[name]["minutes"]:
                row["close"] = "0.000000000000000001"
            doc[name]["minutes"][0]["close"] = "9" * 38
        pearl_sum = "9" * 38 + ".000000000000000059"
        result = self.run_review(doc)
        self.assertEqual((result.returncode, result.stderr), (0, ""), result)
        self.assertIn(f"Mean USDT per PRL (exact sum / minutes): {pearl_sum} / 60", result.stdout)
        self.assertIn("Mean USD per PRL unavailable (exact arithmetic limit exceeded)", result.stdout)
        del doc["fx"]["minutes"][1]
        result = self.run_review(doc)
        self.assertEqual((result.returncode, result.stderr), (0, ""), result)
        self.assertIn(f"Mean USDT per PRL (exact sum / minutes): {pearl_sum} / 60", result.stdout)
        self.assertIn("59/60 closed minutes", result.stdout)
        self.assertIn("Mean USD per PRL unavailable (incomplete aligned FX coverage)", result.stdout)

    def test_unclosed_wrong_or_incomplete_minutes(self):
        for key in ("pearl", "fx"):
            for name, mutation in (
                ("identity", lambda s: s.update(series="private-sentinel")),
                ("duplicate", lambda s: s["minutes"].__setitem__(30, copy.deepcopy(s["minutes"][29]))),
                ("extra", lambda s: s["minutes"].append(copy.deepcopy(s["minutes"][59]))),
                ("unsorted", lambda s: s["minutes"].reverse()),
                ("end exclusive", lambda s: s["minutes"][59].update(start_utc=END)),
                ("before start", lambda s: s["minutes"][0].update(start_utc=START - 60)),
                ("unaligned", lambda s: s["minutes"][0].update(start_utc=START + 1)),
                ("unclosed", lambda s: s["minutes"][0].update(closed=False)),
                ("unclosed type", lambda s: s["minutes"][0].update(closed=1)),
                ("unknown", lambda s: s["minutes"][0].update(unexpected="field")),
                ("early observation", lambda s: s.update(observed_utc=END - 1)),
                ("future observation", lambda s: s.update(observed_utc=int(time.time()) + 3600)),
                ("observation string", lambda s: s.update(observed_utc=str(END + 1))),
            ):
                with self.subTest(series=key, mutation=name):
                    doc = sample()
                    mutation(doc[key])
                    self.assert_refusal(self.run_review(doc), "invalid")
        doc = sample()
        del doc["pearl"]["minutes"][0]
        self.assert_refusal(self.run_review(doc), "requires all 60 closed minutes")

    def test_document_and_price_validation(self):
        for bad in (1, "0", "-1", "01", "2.0", "1e2", " 1", "1" * 39, "0." + "0" * 18 + "1"):
            with self.subTest(close=bad):
                doc = sample()
                doc["pearl"]["minutes"][0]["close"] = bad
                self.assert_refusal(self.run_review(doc), "invalid Pearl")
        for bad in (END + 1, END * 1000, 3600):
            doc = sample()
            doc["end_utc"] = bad
            self.assert_refusal(self.run_review(doc), "UTC minute boundary")
        for values in ({"schema_version": 2}, {"fx": None}, {"extra": "private-sentinel"}):
            doc = sample()
            doc.update(values)
            self.assert_refusal(self.run_review(doc), "invalid")
        text = json.dumps(sample()).replace('"schema_version": 1', '"schema_version": 1, "schema_version": 1')
        self.file.write_text(text, encoding="utf-8")
        self.assert_refusal(self.run_miner("market-hour", "--file", self.file.name), "invalid hour document")

    def test_save_and_recalculate_original_report(self):
        cases = ((sample(), "closed minutes", "Mean USD per PRL (exact sum / minutes): 236.25 / 60"),
                 (vendor_sample(fx_begin=1), "Candle finality is unverified",
                  "Mean USD per PRL unavailable (incomplete aligned FX coverage)"))
        for index, (doc, coverage, usd) in enumerate(cases):
            with self.subTest(schema=doc["schema_version"]):
                original = (json.dumps(doc, indent=2) + "\n").encode()
                self.file.write_bytes(original)
                archive = self.stage / f"hour-{index}.cmh"
                saved = self.run_miner("market-hour", "--file", self.file.name, "--save-review", archive.name)
                self.assertEqual(saved.returncode, 0, saved)
                self.assertIn(coverage, saved.stdout)
                self.assertIn(usd, saved.stdout)
                self.assertIn("Review UTC ", saved.stdout)

                saved_bytes = archive.read_bytes()
                body, digest = saved_bytes[:-65], saved_bytes[-65:]
                self.assertEqual(digest, hashlib.sha256(body).hexdigest().encode() + b"\n")
                header, cutoff, input_size, parts = body.split(b"\n", 3)
                self.assertEqual(header, b"CANARY-MARKET-HOUR-REVIEW-1")
                self.assertEqual(parts[:int(input_size)], original)
                report_size, report = parts[int(input_size):].split(b"\n", 1)
                self.assertEqual(int(report_size), len(report))
                self.assertEqual(report.decode(), saved.stdout)
                self.assertIn(f"Review UTC {int(cutoff)}", saved.stdout)
                if os.name != "nt":
                    self.assertEqual(stat.S_IMODE(archive.stat().st_mode), 0o600)

                self.file.unlink()
                reopened = self.run_miner("market-hour", "--open-review", archive.name)
                self.assertEqual((reopened.returncode, reopened.stdout), (0, saved.stdout), reopened)
                renamed = self.stage / f"review-\u884c\u60c5-{index}.cmh"
                renamed.write_bytes(saved_bytes)
                reopened = self.run_miner("market-hour", "--open-review", str(renamed))
                self.assertEqual((reopened.returncode, reopened.stdout), (0, saved.stdout), reopened)
                self.assertEqual(archive.read_bytes(), saved_bytes)
                self.assertFalse(self.file.exists())
                self.assertFalse(archive.with_suffix(".cmh.tmp").exists())
                self.assertEqual((self.stage / "config.json").read_text(), "CONFIG_SENTINEL")

    def test_replay_rejects_corrupt_or_changed_archives(self):
        self.file.write_text(json.dumps(sample()), encoding="utf-8")
        archive = self.stage / "hour.cmh"
        saved = self.run_miner("market-hour", "--file", self.file.name, "--save-review", archive.name)
        self.assertEqual(saved.returncode, 0, saved)
        complete = archive.read_bytes()
        body = complete[:-65]

        def rehash(data):
            return data + hashlib.sha256(data).hexdigest().encode() + b"\n"

        header, cutoff, parts = body.split(b"\n", 2)
        future = str(int(time.time()) + 3600).encode()
        altered = body.replace(b"236.25 / 60", b"236.26 / 60")
        changed_input = body.replace(b'"2.25"', b'"2.26"', 1)
        self.assertNotEqual(altered, body)
        self.assertNotEqual(changed_input, body)
        cases = (
            (complete[:-1], "archive unavailable or invalid"),
            (altered + complete[-65:], "archive unavailable or invalid"),
            (rehash(altered), "report differs from the recalculated report"),
            (rehash(changed_input), "report differs from the recalculated report"),
            (rehash(b"\n".join((header, future, parts))), "cutoff is in the future"),
            (rehash(b"\n".join((header, b"0" + cutoff, parts))), "archive unavailable or invalid"),
        )
        for data, message in cases:
            with self.subTest(message=message):
                archive.write_bytes(data)
                self.assert_refusal(self.run_miner("market-hour", "--open-review", archive.name), message)
                self.assertEqual(archive.read_bytes(), data)

    def test_archive_publish_never_replaces_a_file(self):
        self.file.write_text(json.dumps(sample()), encoding="utf-8")

        def create():
            return self.run_miner("market-hour", "--file", self.file.name, "--save-review", "hour.cmh")

        archive = self.stage / "hour.cmh"
        temporary = self.stage / "hour.cmh.tmp"
        temporary.write_bytes(b"waiting")
        self.assert_refusal(create(), "could not create")
        self.assertEqual(temporary.read_bytes(), b"waiting")
        self.assertFalse(archive.exists())
        temporary.unlink()

        with ThreadPoolExecutor(max_workers=2) as pool:
            attempts = [pool.submit(create) for _ in range(2)]
            results = sorted((attempt.result() for attempt in attempts), key=lambda result: result.returncode)
        self.assertEqual([result.returncode for result in results], [0, 3], results)
        self.assert_refusal(results[1], "could not create")
        self.assertFalse(temporary.exists())
        original = archive.read_bytes()
        self.assert_refusal(create(), "existing archive was preserved")
        self.assertEqual(archive.read_bytes(), original)

        incomplete = vendor_sample(pearl_begin=1)
        self.file.write_text(json.dumps(incomplete), encoding="utf-8")
        archive.unlink()
        self.assert_refusal(create(), "requires all 60 elapsed minute buckets")
        self.assertFalse(archive.exists())
        self.assertFalse(temporary.exists())

    def test_archive_options_exclude_public_sampling_and_unsafe_names(self):
        for args in (("--open-review",), ("--open-review", ""),
                     ("--open-review", "hour.cmh", "--allow-public-candles"),
                     ("--allow-public-candles", "--save-review", "../hour.cmh"),
                     ("--file", self.file.name, "--save-review", "bad/name.cmh"),
                     ("--file", self.file.name, "--save-review", "CON.cmh"),
                     ("--file", self.file.name, "--save-review", "not-an-hour.cmr")):
            with self.subTest(args=args):
                self.assert_refusal(self.run_miner("market-hour", *args), code=2)

    def test_nonregular_missing_and_oversized_input(self):
        def run_file(path):
            self.assert_refusal(self.run_miner("market-hour", "--file", str(path)), "input file unavailable")

        run_file(self.file)
        self.file.write_bytes(b" " * (64 * 1024 + 1))
        run_file(self.file)
        directory = self.stage / "directory"
        directory.mkdir()
        run_file(directory)
        if os.name != "nt":
            link = self.stage / "link"
            link.symlink_to(self.file)
            run_file(link)
            pipe = self.stage / "pipe"
            os.mkfifo(pipe)
            run_file(pipe)


if __name__ == "__main__":
    unittest.main()
