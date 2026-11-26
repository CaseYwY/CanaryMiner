"""Offline ticker review through the ordinary miner commands on either build profile."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

CUTOFF = 1760000060


def run_suite(cli):
    with TemporaryDirectory(prefix="canary-declared-tickers-") as tmp:
        root = Path(tmp)
        path = root / "private \u03a9 price snapshot.json"
        config = root / "config.json"
        config.write_text("private-sentinel", encoding="ascii")
        gate = [{"currency_pair": "PEARL_USDT", "last": "2.500", "highest_bid": "2.49",
                 "lowest_ask": "2.51", "etf_pre_timestamp": 123}]
        fx = {"error": [], "result": {"USDTZUSD": {"c": ["0.98", "1"]}}}
        document = {"schema_version": 1, "as_of_utc": CUTOFF,
                    "gate": {"observed_utc": CUTOFF - 120, "response_json": json.dumps(gate)},
                    "kraken": {"observed_utc": CUTOFF - 90, "response_json": json.dumps(fx)}}
        used = 0

        def call(expected, *args):
            nonlocal used
            proc = subprocess.run([cli, "market-tickers", *args], cwd=root, capture_output=True, text=True, timeout=10)
            assert proc.returncode == expected, proc
            assert all(private not in proc.stdout + proc.stderr for private in
                       ("private-sentinel", "private", "FORBIDDEN_DEVICE_SETUP", "etf_pre_timestamp")), proc
            assert config.read_text(encoding="ascii") == "private-sentinel"
            used += 1
            return proc

        def review(record, expected=0):
            source = json.dumps(record).encode("ascii")
            path.write_bytes(source)
            proc = call(expected, "--file", path)
            assert path.read_bytes() == source
            return proc

        flags = call(0, "--help")
        assert "market-tickers --file FILE" in flags.stdout
        for args in (("--file",), ("--file", "--allow-public-tickers"),
                     ("--allow-public-tickers", "--file", path), ("--fil\uff45", path),
                     ("--file", path, "--allow-public-tickers")):
            assert not call(2, *args).stdout

        first = review(document)
        assert not first.stderr
        assert "Historical ticker review from declared payloads" in first.stdout
        assert "last-trade values" in first.stdout
        assert "age at cutoff 120 s" in first.stdout and "age at cutoff 90 s" in first.stdout
        assert "Declared PRL/USD reference at cutoff: 2.45" in first.stdout
        assert first.stdout == call(0, "--file", path).stdout

        earlier = deepcopy(document)
        earlier["gate"]["observed_utc"] -= 1
        proc = review(earlier, 3)
        assert "outside declared age limit" in proc.stdout and "Declared Kraken USDT/USD: 0.98" in proc.stdout
        assert "Declared PRL/USD reference at cutoff: N/A" in proc.stdout

        skewed = deepcopy(document)
        skewed["kraken"]["observed_utc"] += 1
        proc = review(skewed, 3)
        assert "observations more than 30 s apart" in proc.stdout
        assert "Declared Gate Pearl mainnet PRL/USDT: 2.5" in proc.stdout

        only = deepcopy(document)
        del only["kraken"]
        assert "N/A (source not supplied)" in review(only, 3).stdout
        only = deepcopy(document)
        del only["gate"]
        assert "Declared Kraken USDT/USD: 0.98" in review(only, 3).stdout

        mismatch = deepcopy(document)
        mismatch["gate"]["response_json"] = json.dumps([{"currency_pair": "PRL_USDT", "last": "2.50"}])
        bad = review(mismatch, 3)
        assert "Declared Gate Pearl mainnet PRL/USDT: N/A" in bad.stdout
        assert "Declared Kraken USDT/USD: 0.98" in bad.stdout

        mismatch = deepcopy(document)
        mismatch["kraken"]["response_json"] = '{"error":["EAPI:Rate limit exceeded"]}'
        bad = review(mismatch, 3)
        assert "provider rate limit envelope" in bad.stdout and "EAPI:" not in bad.stdout

        for record in (
            {**document, "secret": "private-sentinel"},
            {**document, "schema_version": 2},
            {"schema_version": 1, "as_of_utc": CUTOFF},
            {**document, "as_of_utc": 253402300799},
            {**document, "gate": {"observed_utc": CUTOFF + 1, "response_json": "[]"}},
            {**document, "gate": {"observed_utc": CUTOFF, "response_json": []}},
        ):
            failed = review(record, 3)
            assert failed.stderr and not failed.stdout

        path.write_bytes(b"x" * (64 * 1024 + 1))
        assert "oversized" in call(3, "--file", path).stderr
        assert not call(3, "--file", root).stdout
        print(f"Declared ticker file review passed {used} command cases")


if __name__ == "__main__":
    run_suite(Path(sys.argv[1]).resolve())
