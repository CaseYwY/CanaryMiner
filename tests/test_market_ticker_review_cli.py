"""Offline ticker review through the ordinary miner commands on either build profile."""
from copy import deepcopy
from hashlib import sha256
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

        def report_at(cutoff, gate_clock, fx_clock):
            gate_sent, gate_observed, gate_received = gate_clock[:3]
            fx_sent, fx_observed, fx_received = fx_clock[:3]
            return (
                "Historical public ticker capture (ages at capture)\n"
                f"Capture UTC {cutoff} | no trade-event timestamps or current freshness claim\n"
                f"Gate Pearl mainnet PRL/USDT: 2.5 | observed UTC {gate_observed} | age at capture {cutoff - gate_observed} s\n"
                f"  Request start UTC {gate_sent} | receipt UTC {gate_received}\n"
                f"Kraken USDT/USD: 0.98 | observed UTC {fx_observed} | age at capture {cutoff - fx_observed} s\n"
                f"  Request start UTC {fx_sent} | receipt UTC {fx_received}\n"
                "PRL/USD reference at capture: 2.45\n"
            )

        archived_report = report_at(CUTOFF + 2, (CUTOFF, CUTOFF, CUTOFF + 1),
                                    (CUTOFF + 1, CUTOFF + 1, CUTOFF + 2))
        gate_body = json.dumps(gate).encode("ascii")
        fx_body = json.dumps(fx).encode("ascii")
        archive = root / "private \u03a9 capture.cmt"

        def serialize(start=CUTOFF, end=CUTOFF + 2, elapsed=2000,
                      gate_receipt=(CUTOFF, CUTOFF, CUTOFF + 1, 1000),
                      fx_receipt=(CUTOFF + 1, CUTOFF + 1, CUTOFF + 2, 2000),
                      original_gate=gate_body, summary=archived_report):
            def record(clock, payload):
                sent, observed, received, ms = clock
                return f"{sent}\n{observed}\n{received}\n{ms + 1}\n{len(payload)}\n".encode() + payload
            report = summary.encode("ascii")
            source = (f"CANARY-MARKET-TICKER-REVIEW-1\n{start}\n{end}\n{elapsed + 1}\n".encode() +
                      record(gate_receipt, original_gate) + record(fx_receipt, fx_body) +
                      f"{len(report)}\n".encode() + report)
            return source + sha256(source).hexdigest().encode() + b"\n"

        archive.write_bytes(serialize())
        assert call(0, "--open-review", archive).stdout == archived_report
        assert call(0, "--open-review", archive).stdout == archived_report
        assert archive.read_bytes() == serialize()
        boundary_gate = (CUTOFF, CUTOFF + 1, CUTOFF + 1, 1000)
        boundary_fx = (CUTOFF + 31, CUTOFF + 31, CUTOFF + 31, 31000)
        boundary_report = report_at(CUTOFF + 31, boundary_gate, boundary_fx)
        archive.write_bytes(serialize(end=CUTOFF + 31, elapsed=31000,
                                      gate_receipt=boundary_gate, fx_receipt=boundary_fx, summary=boundary_report))
        assert call(0, "--open-review", archive).stdout == boundary_report
        for invalid in (
            serialize()[:-4], serialize().replace(b"2.500", b"3.500", 1),
            serialize(summary=archived_report.replace("2.45", "2.46")),
            serialize(original_gate=gate_body.replace(b"2.500", b"3.500")),
            serialize(original_gate=gate_body.replace(b"PEARL_USDT", b"OTHER_USDT")),
            serialize(fx_receipt=(CUTOFF - 1, CUTOFF + 1, CUTOFF + 2, 2000)),
            serialize(gate_receipt=(CUTOFF, CUTOFF - 1, CUTOFF + 1, 1000)),
            serialize(fx_receipt=(CUTOFF + 1, CUTOFF + 1, CUTOFF + 2, 900)),
            serialize(elapsed=40000),
            serialize(end=CUTOFF + 32, elapsed=32000,
                      fx_receipt=(CUTOFF + 31, CUTOFF + 31, CUTOFF + 32, 32000)),
            serialize(end=CUTOFF + 31, elapsed=31001, gate_receipt=boundary_gate,
                      fx_receipt=(*boundary_fx[:3], 31001), summary=boundary_report),
            serialize(end=253402300799),
            serialize().replace(b"REVIEW-1", b"REVIEW-2", 1),
            b"x" * (2 * 1024 * 1024 + 8192 + 1025),
        ):
            archive.write_bytes(invalid)
            failed = call(3, "--open-review", archive)
            assert not failed.stdout and failed.stderr, failed
            assert archive.read_bytes() == invalid
        assert not call(3, "--open-review", root).stdout
        print(f"Declared ticker file review passed {used} command cases")


if __name__ == "__main__":
    run_suite(Path(sys.argv[1]).resolve())
