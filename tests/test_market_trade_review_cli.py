"""Check public trade-event references and the standalone saved review."""
from hashlib import sha256
from io import BytesIO
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory
import time


HEADER = b"CANARY-MARKET-TRADE-REVIEW-1\n"
ALLOW = "--allow-public-trades"
RESOLVER = "192.168.50.53"


def gate_row(seconds, price="2.50", ident="2"):
    return {"id": ident, "create_time": str(seconds), "create_time_ms": f"{seconds * 1000 + 123}.4567",
            "currency_pair": "PEARL_USDT", "side": "buy", "amount": "0.005", "price": price,
            "sequence_id": ident}


def gate_body(*rows):
    return b"\t" + json.dumps(rows, separators=(",", ":")).encode("ascii") + b"\r\n "


def kraken_body(*rows, pair="USDTZUSD"):
    payloads = [f'[{json.dumps(price)},"1",{stamp},"b","m","",{ident}]' for price, stamp, ident in rows]
    return (f'{{"error":[],"result":{{"{pair}":[{",".join(payloads)}],"last":"123"}}}}\n').encode("ascii")


def sign_archive(content):
    return content + sha256(content).hexdigest().encode("ascii") + b"\n"


def offline_original():
    start = 1760000000
    gate = gate_body(gate_row(start - 5))
    kraken = kraken_body(("0.98", f"{start - 4}.246000001", 3))
    cutoff = start * 1000 + 300
    report = ("Public trade sample (newest timestamp in each returned batch)\n"
              f"Capture UTC ms {cutoff} | ages at capture\n"
              f"Gate Pearl mainnet PRL/USDT: 2.5 | event UTC {start - 5}.123456700 | receipt UTC ms {start*1000+100}"
              " | event age 5177 ms | receipt age 200 ms\n"
              f"Kraken USDT/USD: 0.98 | event UTC {start - 4}.246000001 | receipt UTC ms {start*1000+200}"
              " | event age 4054 ms | receipt age 100 ms\n"
              "PRL/USD trade reference at capture: 2.45\n")

    def record(begin_ms, receipt_ms, body):
        return f"{start*1000+begin_ms}\n{start}\n{start*1000+receipt_ms}\n{receipt_ms+1}\n{len(body)}\n".encode() + body

    raw = (HEADER + f"{start*1000}\n{cutoff}\n301\n".encode() + record(10, 100, gate) +
           record(110, 200, kraken) + f"{len(report)}\n".encode() + report.encode())
    return sign_archive(raw), report


def offline_partial():
    start = 1760000000000
    gate = gate_body(gate_row(start // 1000 - 5))
    report = ("Public trade sample (newest timestamp in each returned batch)\n"
              f"Capture UTC ms {start + 300} | ages at capture\n"
              f"Gate Pearl mainnet PRL/USDT: 2.5 | event UTC 1759999995.123456700 | receipt UTC ms {start + 100}"
              " | event age 5177 ms | receipt age 200 ms\n"
              f"  Request start UTC ms {start + 10} | receipt UTC ms {start + 100}\n"
              "Kraken USDT/USD: N/A (public provider HTTP error)\n"
              f"  Request start UTC ms {start + 110} | receipt UTC ms {start + 200} | HTTP 429 | Retry-After 1 s\n"
              "PRL/USD trade reference at capture: N/A (requires two valid trades and receipts within 120 s)\n")

    def record(begin_ms, receipt_ms, wire):
        return f"{start + begin_ms}\n{start + receipt_ms}\n{receipt_ms + 1}\n{len(wire)}\n".encode() + wire

    ticker_wire = f"CMJSON1 0 200 {start // 1000} 0 {len(gate)}\n".encode() + gate
    limited_wire = b"CMJSON1 10 429 0 1 0\n"
    raw = (b"CANARY-MARKET-TRADE-SAMPLE-1\n" + f"{start}\n{start + 300}\n301\n".encode() +
           record(10, 100, ticker_wire) + record(110, 200, limited_wire) +
           f"{len(report)}\n".encode() + report.encode())
    return sign_archive(raw), report


def main(cli_path, fixture, capable):
    with TemporaryDirectory(prefix="canary-public-trade-") as temp:
        root = Path(temp)
        dest = root / "installed"
        work = root / "private work"
        for directory in (dest, work):
            directory.mkdir()
        cli = Path(shutil.copy2(cli_path, dest / cli_path.name))
        helper = dest / ("canary_public_json_worker" + (".exe" if os.name == "nt" else ""))
        requests, dns = dest / "trade-requests", dest / "resolver-requests"
        archive = work / "case.cmt"
        temporary = work / "case.cmt.tmp"
        scenario = dest / "trade-scenario"
        private = work / "config.json"
        private.write_text("private-sentinel", encoding="ascii")

        def call(code, *args):
            result = subprocess.run([cli, "market-tickers", *args], cwd=work, capture_output=True,
                                    text=True, timeout=8)
            assert result.returncode == code, result
            assert all(value not in result.stdout + result.stderr for value in
                       ("private-sentinel", "FORBIDDEN_DEVICE_SETUP", RESOLVER)), result
            return result

        def assert_requests():
            assert requests.read_text(encoding="ascii").splitlines() == ["7", "8"]
            requests.unlink()
            assert not (dest / "ticker-requests").exists()

        supplied, expected = offline_original()
        archive.write_bytes(supplied)
        assert call(0, "--open-review", archive).stdout == expected
        damaged = supplied[:-65].replace(b'"price":"2.50"', b'"price":"2.51"')
        assert damaged != supplied[:-65]
        archive.write_bytes(sign_archive(damaged))
        changed = call(3, "--open-review", archive)
        assert not changed.stdout and "differs from the recalculated report" in changed.stderr
        future = supplied[:-65].replace(b'"create_time":"1759999995"', b'"create_time":"1760000000"')
        future = future.replace(b'"create_time_ms":"1759999995123.4567"', b'"create_time_ms":"1760000000100.0001"')
        assert future != supplied[:-65] and len(future) == len(supplied) - 65
        archive.write_bytes(sign_archive(future))
        assert "incomplete fresh trade pair" in call(3, "--open-review", archive).stderr
        invalid_utf8 = supplied[:-65].replace(b'"side":"buy"', b'"side":"\xffuy"')
        assert invalid_utf8 != supplied[:-65] and len(invalid_utf8) == len(supplied) - 65
        archive.write_bytes(sign_archive(invalid_utf8))
        assert "incomplete fresh trade pair" in call(3, "--open-review", archive).stderr
        partial, expected_partial = offline_partial()
        archive.write_bytes(partial)
        assert call(0, "--open-review", archive).stdout == expected_partial
        forged = partial[:-65].replace(b"CMJSON1 10 429", b"CMJSON1 10 503")
        archive.write_bytes(sign_archive(forged))
        assert "differs from the recalculated report" in call(3, "--open-review", archive).stderr
        noncanonical = partial[:-65].replace(b"CMJSON1 10", b"CMJSON1\t10")
        archive.write_bytes(sign_archive(noncanonical))
        assert "archive unavailable or invalid" in call(3, "--open-review", archive).stderr
        archive.unlink()

        shutil.copy2(fixture, helper)
        assert ALLOW in call(0, "--help").stdout
        for args in ((ALLOW, ALLOW), (ALLOW, "--allow-public-tickers"), (ALLOW, "--file", private),
                     (ALLOW, "--save-review", "../outside.cmt"), ("--allow-public-trade\uff53",),
                     (ALLOW, "--sav\uff45-review", archive.name), (ALLOW, "--sav\uff45-sample", archive.name),
                     (ALLOW, "--save-review", archive.name, "--save-sample", archive.name), (ALLOW, "--unknown")):
            assert not call(2, *args).stdout and not requests.exists()
        if not capable:
            disabled = call(3, ALLOW, "--save-review", archive.name)
            assert "HTTPS unavailable in this build" in disabled.stderr and not disabled.stdout
            disabled = call(3, ALLOW, "--save-sample", archive.name)
            assert "HTTPS unavailable in this build" in disabled.stderr and not disabled.stdout
            assert not requests.exists() and not archive.exists()
            print("Trade CLI: offline replay and disabled public fetch passed")
            return 0
        if sys.platform == "linux":
            libc, version = os.confstr("CS_GNU_LIBC_VERSION").split()
            if libc != "glibc" or tuple(map(int, version.split(".")[:2])) < (2, 40):
                return 77

        now = int(time.time())
        latest = gate_row(now - 2)
        older = gate_row(now - 10, "70", "1")
        gate = gate_body(latest, older)
        fx = kraken_body(("20", f"{now - 11}.123", 3), ("0.98", f"{now - 3}.456789001", 4))

        def payloads(gate_original=gate, kraken_original=fx):
            (dest / "trade-response-7.json").write_bytes(gate_original)
            (dest / "trade-response-8.json").write_bytes(kraken_original)

        payloads()
        first = call(0, ALLOW)
        assert "PRL/USD trade reference at capture: 2.45" in first.stdout
        assert f"event UTC {now-2}.123456700" in first.stdout and f"event UTC {now-3}.456789001" in first.stdout
        assert "receipt age " in first.stdout and "event age " in first.stdout and not first.stderr
        assert_requests()
        identity_requests = dest / "identity-requests"
        assert identity_requests.read_text(encoding="ascii").splitlines() == ["12"]
        identity_requests.unlink()
        identity_scenario = dest / "identity-scenario"
        identity_scenario.write_text("perle", encoding="ascii")
        denied = call(3, ALLOW)
        assert "public market asset identity could not be verified" in denied.stdout
        assert "PRL/USD trade reference at capture: 2.45" not in denied.stdout
        assert requests.read_text(encoding="ascii").splitlines() == ["8"]
        requests.unlink()
        assert identity_requests.read_text(encoding="ascii").splitlines() == ["12"]
        identity_requests.unlink()
        identity_scenario.write_text("stale-clock", encoding="ascii")
        denied = call(3, ALLOW)
        assert "public market asset observation time could not be verified" in denied.stdout
        assert "PRL/USD trade reference at capture: N/A" in denied.stdout
        assert requests.read_text(encoding="ascii").splitlines() == ["8"]
        requests.unlink()
        assert identity_requests.read_text(encoding="ascii").splitlines() == ["12"]
        identity_requests.unlink()
        identity_scenario.unlink()

        save = (ALLOW, "--save-review", archive.name)
        captured = call(0, *save, "--dns-server", RESOLVER)
        assert_requests()
        assert dns.read_text(encoding="ascii").splitlines() == [RESOLVER, RESOLVER]
        dns.unlink()
        stored = archive.read_bytes()
        fields = BytesIO(stored)
        assert fields.readline() == HEADER.replace(b"REVIEW-1", b"REVIEW-2") and all(int(fields.readline()) > 0 for _ in range(3))
        for body in (gate, fx):
            assert all(int(fields.readline()) > 0 for _ in range(4))
            assert fields.read(int(fields.readline())) == body
        assert fields.readline() == b"2\n"
        observed = int(fields.readline())
        metadata = fields.read(int(fields.readline()))
        assert json.loads(metadata)["chain"] == "PEARL"
        assert fields.read(int(fields.readline())) == captured.stdout.encode("ascii")
        assert f"Gate Pearl asset metadata: identity checked | observed UTC {observed}" in captured.stdout
        assert RESOLVER.encode() not in stored
        assert "existing archive was preserved" in call(3, *save).stderr
        assert_requests()
        assert archive.read_bytes() == stored and not temporary.exists()

        for gate_data, kraken_data, diagnostic in (
            (gate_body({**latest, "currency_pair": "private-sentinel"}), fx, "identity"),
            (gate_body({**latest, "trade_quote": "USD"}), fx, "identity"),
            (gate_body({**latest, "create_time": str(now - 3)}), fx, "timestamp"),
            (gate_body(gate_row(now + 600)), fx, "timestamp"),
            (gate_body(gate_row(now - 121)), fx, "outside age limit"),
            (gate_body(gate_row(now - 40)), fx, "more than 30 s apart"),
            (gate_body(latest, latest), fx, "schema"),
            (gate_body(latest, {**latest, "id": "5", "price": "1"}), fx, "coverage"),
            (gate, kraken_body(("0.98", f"{now - 3}.12", 4), pair="private-sentinel"), "identity"),
            (gate, kraken_body(("0.98", f'"{now - 3}.12"', 4)), "timestamp"),
            (gate, kraken_body(("0", f"{now - 3}.12", 4)), "decimal"),
            (b"[]", fx, "coverage"),
        ):
            payloads(gate_data, kraken_data)
            report = call(3, ALLOW)
            assert diagnostic in report.stdout.lower() and "PRL/USD trade reference at capture: N/A" in report.stdout
            assert_requests()
        payloads()
        scenario.write_text("future", encoding="ascii")
        future = call(3, ALLOW)
        assert future.stdout.count("invalid trade or receipt timestamp") == 2
        assert_requests()
        scenario.write_text("limited", encoding="ascii")
        limited = call(3, ALLOW)
        assert "Kraken USDT/USD: N/A (public provider HTTP error)" in limited.stdout
        assert_requests()
        failed = call(3, ALLOW, "--save-review", "incomplete.cmt")
        assert "both valid, fresh, aligned trades required" in failed.stderr and not failed.stdout
        assert not (work / "incomplete.cmt").exists()
        assert_requests()
        partial_path = work / "incomplete.cmt"
        saved = call(3, ALLOW, "--save-sample", partial_path.name, "--dns-server", RESOLVER)
        assert not saved.stderr and "HTTP 429 | Retry-After 1 s" in saved.stdout
        assert "Gate Pearl mainnet PRL/USDT: 2.5" in saved.stdout and "reference at capture: N/A" in saved.stdout
        saved_bytes = partial_path.read_bytes()
        assert saved_bytes.startswith(b"CANARY-MARKET-TRADE-SAMPLE-2\n") and RESOLVER.encode() not in saved_bytes
        assert dns.read_text(encoding="ascii").splitlines() == [RESOLVER, RESOLVER]
        dns.unlink()
        assert_requests()
        helper.unlink()
        assert call(0, "--open-review", archive).stdout == captured.stdout
        assert call(0, "--open-review", partial_path).stdout == saved.stdout and partial_path.read_bytes() == saved_bytes
        assert not requests.exists() and not dns.exists()
        assert archive.read_bytes() == stored and private.read_text() == "private-sentinel"
    print("Trade CLI: selection, independent freshness and skew, consent, capture and offline replay passed")
    return 0


if __name__ == "__main__":
    sys.exit(main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), sys.argv[3] == "1"))
