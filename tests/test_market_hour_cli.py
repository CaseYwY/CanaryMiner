"""Exercise the ordinary offline command with the actual miner's host CLI."""
import copy
import json
import os
from pathlib import Path
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
