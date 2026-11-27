"""One-shot hour sample using only an inert installed sibling."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time


def main(cli_path, fixture, enabled, sample_only=False):
    worker_name = "canary_public_json_worker" + (".exe" if os.name == "nt" else "")
    libc = os.confstr("CS_GNU_LIBC_VERSION") if sys.platform == "linux" else None
    older_linux = libc and tuple(map(int, libc.split()[1].split(".")[:2])) < (2, 40)
    allow = "--allow-public-candles"
    fallback = "--kraken-fallback"
    with tempfile.TemporaryDirectory(prefix="canary-hour-sample-") as directory:
        root = Path(directory)
        installed = root / "installed"
        operator = root / "operator"
        installed.mkdir()
        operator.mkdir()
        config = operator / "config.json"
        config.write_text("private-sentinel", encoding="ascii")
        cli = Path(shutil.copy2(cli_path, installed / cli_path.name))
        helper = installed / worker_name
        shutil.copy2(fixture, helper)
        shutil.copy2(fixture, operator / worker_name)
        if not sample_only:
            preferences = "canary_preferences_worker" + (".exe" if os.name == "nt" else "")
            shutil.copy2(cli_path.parent / preferences, installed / preferences)
        requests = installed / "candle-requests"
        resolver_requests = installed / "resolver-requests"
        resolver = "fd00::53"
        dns = ("--dns-server", resolver)
        scenario = installed / "candle-scenario"
        archive = operator / "public.cmh"
        save = (allow, "--save-review", archive.name)
        temporary = archive.with_suffix(".cmh.tmp")
        sample_archive = operator / "sample.cmh"
        capture = (allow, "--save-sample", sample_archive.name)

        def call(command, *args):
            result = subprocess.run([cli, command, *args], cwd=operator, capture_output=True, text=True, timeout=8)
            assert all(private not in result.stdout + result.stderr for private in
                       ("private-sentinel", "FORBIDDEN_DEVICE_SETUP")), result
            return result

        def run(*args):
            return call("market-hour", *args)

        def launched(expected):
            rows = [line.split() for line in requests.read_text(encoding="ascii").splitlines()]
            assert [row[0] for row in rows] == expected, rows
            intervals = {(int(row[1]), int(row[2])) for row in rows}
            assert len(intervals) == 1, rows
            start, end = intervals.pop()
            assert start % 60 == end % 60 == 0 and end - start == 3600
            assert 5 <= int(time.time()) - end <= 90
            requests.unlink()
            return start, end

        for flag in ("--help", "-h"):
            result = run(flag)
            assert result.returncode == 0 and "market-hour " + allow in result.stdout and "--save-sample" in result.stdout
        for arguments in ((), (allow, allow), (allow, "--no-market"), (allow, "--config", config.name),
                          ("--file", config.name, allow), (allow + "=true",), ("--help", allow),
                          ("--allow-public-candle\uff53",), ("--url", "private-sentinel"), dns,
                          (allow, "--dns-server"), (allow, "--dns-server", "hostname"),
                          (allow, *dns, *dns), ("--file", config.name, *dns),
                          ("--open-review", archive.name, *dns), (allow, "--dn\uff53-server", resolver),
                          (fallback,), (allow, fallback, fallback), (allow, fallback + "=true"),
                          (allow, "--kraken-fallbac\u212a"), ("--open-review", archive.name, fallback),
                          ("--save-sample", sample_archive.name), (allow, "--save-sample"),
                          (*capture, "--save-review", archive.name), (*capture, *capture[1:]),
                          (allow, "--save-sampl\uff45", sample_archive.name), (allow, "--save-sample", "../bad.cmh")):
            result = run(*arguments)
            assert result.returncode == 2 and not result.stdout and not requests.exists(), result
        result = call("market-hou\uff52", allow)
        assert result.returncode == 2 and not result.stdout and not requests.exists(), result

        ticker = "--allow-public-tickers"
        trades = "--allow-public-trades"
        run_help = call("run", "--help")
        assert run_help.returncode == 0 and trades in run_help.stdout
        assert any(allow in line and "with either quote flag" in line for line in run_help.stdout.splitlines())
        dashboard_dns = "--market-dns-server"
        dns_required = "--market-dns-server requires --allow-public-tickers at the Dashboard parent"
        dns_usage = "--market-dns-server requires one unicast IPv4 or unscoped IPv6 address for run"
        dashboard_refusals = (
            ([trades, trades], "--allow-public-trades is accepted once, for run only"),
            ([trades + "=true"], "--allow-public-trades takes no value"),
            ([ticker, trades], "cannot be combined with --allow-public-tickers"),
            ([trades, "--worker-child"], "--allow-public-trades requires the Dashboard parent"),
            (["--pass", trades, dashboard_dns, resolver], dns_required),
            ([allow], "Dashboard candles require --allow-public-tickers as well"),
            ([ticker, allow, allow], "--allow-public-candles is accepted once, for run only"),
            ([ticker, allow + "=true"], "--allow-public-candles takes no value"),
            ([ticker, fallback], "--kraken-fallback requires --allow-public-candles at the Dashboard parent"),
            ([ticker, allow, fallback, fallback], "--kraken-fallback is accepted once without a value"),
            ([ticker, allow, fallback + "=true"], "--kraken-fallback is accepted once without a value"),
            ([ticker, allow, fallback, "--worker-child"], "--kraken-fallback requires --allow-public-candles at the Dashboard parent"),
            (["--pass", ticker, allow], "Dashboard candles require --allow-public-tickers as well"),
            ([dashboard_dns, resolver], dns_required),
            (["--pass", ticker, dashboard_dns, resolver], dns_required),
            ([ticker, dashboard_dns], dns_usage),
            ([ticker, dashboard_dns, "private-sentinel"], dns_usage),
            ([ticker, dashboard_dns, "fe80::53%1"], dns_usage),
            ([ticker, dashboard_dns + "=" + resolver], dns_usage),
            ([ticker, dashboard_dns, resolver, dashboard_dns, resolver], dns_usage),
            ([ticker, dashboard_dns, "--worker", "fixture-name", resolver], dns_usage),
            ([ticker, dashboard_dns, resolver, "--worker-child"], dns_required),
        )
        # Native installs only exercise the one-shot command; run refusals use the inert host binary.
        for args, expected in (() if sample_only else dashboard_refusals):
            result = subprocess.run([cli, "run", "--pool", "kryptex", "--wallet", "prl1" + "q" * 59,
                                     *args], cwd=installed, capture_output=True, text=True, timeout=8)
            assert result.returncode == 2 and not result.stdout and expected in result.stderr, result
            assert resolver not in result.stderr and "private-sentinel" not in result.stderr
            assert not requests.exists() and not (installed / "ticker-requests").exists()
        if not sample_only:
            positive = call("run", ticker, allow, dashboard_dns, resolver, "--help")
            assert positive.returncode == 0 and dashboard_dns in positive.stdout
            assert resolver not in positive.stdout + positive.stderr
            for source in (ticker, trades):
                admitted = subprocess.run([cli, "run", "--algo", "pearl-pow", "--pool", "kryptex",
                                           "--wallet", "prl1" + "q" * 59, "--no-donation",
                                           source, allow, fallback, dashboard_dns, resolver],
                                          cwd=operator, capture_output=True, text=True, timeout=8)
                if older_linux:
                    assert admitted.returncode == 3 and "Pre-run preferences unavailable" in admitted.stderr, admitted
                    assert "FORBIDDEN_DEVICE_SETUP" not in admitted.stderr, admitted
                else:
                    assert admitted.returncode == 97 and "FORBIDDEN_DEVICE_SETUP" in admitted.stderr, admitted
                assert resolver not in admitted.stdout + admitted.stderr
            for args in (("--allow-public-ticker\uff53", dashboard_dns, resolver),
                         ("--allow-public-trade\uff53", dashboard_dns, resolver),
                         (ticker, "\u00ad-market-dns-server", resolver),
                         (ticker, "--market-dns-\u017ferver", resolver),
                         (ticker, allow, "--kraken-fallbac\u212a")):
                rejected = call("run", *args)
                assert rejected.returncode in (1, 2) and not rejected.stdout
            for command in ("status", "version", "settings", "list-devices", "selftest"):
                rejected = call(command, dashboard_dns, resolver)
                assert rejected.returncode == 2 and "--market-dns-server is for run only" in rejected.stderr
            for alias in ("--version", "--list-devices"):
                rejected = call(dashboard_dns, resolver, alias)
                assert rejected.returncode == 2 and "--market-dns-server is for run only" in rejected.stderr
                rejected = call(alias, dashboard_dns, resolver)
                assert rejected.returncode == 2 and "--market-dns-server is for run only" in rejected.stderr
            assert not requests.exists() and not (installed / "ticker-requests").exists()

        assert not resolver_requests.exists()
        if not enabled:
            for args in ((allow,), save, capture, (allow, *dns), (allow, fallback, *dns)):
                result = run(*args)
                assert result.returncode == 3 and not result.stdout and "HTTPS unavailable in this build" in result.stderr
            assert not requests.exists()
            assert not archive.exists() and not sample_archive.exists() and not resolver_requests.exists()
            print("Public hour: default build refuses network opt-in")
            return 0
        result = run(allow)
        unavailable = "verified HTTPS/c-ares backend unavailable"
        if sys.platform == "linux" and not requests.exists() and unavailable in result.stdout:
            assert older_linux, result
            return 77
        assert result.returncode == 0 and not result.stderr, result
        start, end = launched(["4", "5"])
        assert f"UTC seconds [{start}, {end})" in result.stdout
        assert re.search(r"window ended [0-9]+ s ago", result.stdout)
        assert "candle finality is unverified" in result.stdout
        assert result.stdout.count("60/60 elapsed minute buckets") == 2
        assert "180 / 60" in result.stdout and "210 / 60" in result.stdout
        assert len(re.findall(r"observed UTC [0-9]+ \(receipt age [0-9]+ s\)", result.stdout)) == 2

        assert not resolver_requests.exists()
        for args in ((allow, *dns), (*save, *dns), (allow, *dns, *save[1:])):
            resolved = run(*args)
            assert resolved.returncode == 0 and not resolved.stderr and resolver not in resolved.stdout, resolved
            assert "180 / 60" in resolved.stdout and "210 / 60" in resolved.stdout
            launched(["4", "5"])
            assert resolver_requests.read_text(encoding="ascii").splitlines() == [resolver, resolver]
            resolver_requests.unlink()
            if "--save-review" in args:
                assert resolver.encode("ascii") not in archive.read_bytes()
                replay = run("--open-review", archive.name)
                assert replay.returncode == 0 and replay.stdout == resolved.stdout and not replay.stderr, replay
                archive.unlink()
            assert not requests.exists() and not resolver_requests.exists()

        for mode, expected in (
            ("gap-fx", "59/60 elapsed minute buckets"),
            ("gap-pearl", "59/60 elapsed minute buckets"),
            ("malformed", "invalid minute response schema"),
            ("future", "invalid response observation time"),
            ("stale", "invalid response observation time"),
            ("limited", "provider rate limited"),
            ("blocked", "non-public destination refused"),
        ):
            scenario.write_text(mode, encoding="ascii")
            result = run(allow)
            assert result.returncode == 3 and not result.stderr and expected in result.stdout, result
            assert "210 / 60" not in result.stdout and "Mean USD per PRL (exact sum / minutes): N/A" in result.stdout
            assert ("180 / 60" in result.stdout) == (mode != "gap-pearl")
            launched(["4", "5"])
            result = run(*save)
            assert result.returncode == 3 and not result.stdout and "complete aligned" in result.stderr, result
            assert expected in result.stderr and "UTC seconds [" in result.stderr, result
            assert "MEXC Pearl mainnet PRL/USDT:" in result.stderr and "Coinbase USDT/USD:" in result.stderr
            assert "Mean " not in result.stderr and "180 / 60" not in result.stderr
            launched(["4", "5"])
            assert not archive.exists() and not temporary.exists()

        scenario.write_text("archive", encoding="ascii")
        saved = run(*save, fallback)
        assert saved.returncode == 0 and not saved.stderr, saved
        start, end = launched(["4", "5"])
        data = archive.read_bytes()
        body = data[:-65]

        def signed(contents):
            return contents + hashlib.sha256(contents).hexdigest().encode("ascii") + b"\n"

        assert len(data) > 65536 and data == signed(body)
        header, initial, saved_end, rest = body.split(b"\n", 3)
        assert header == b"CANARY-MARKET-HOUR-REVIEW-2" and int(saved_end) == end
        assert (int(initial) - 5) // 60 * 60 == end
        clock = int(initial)
        timings = {}
        for side in ("pearl", "fx"):
            sent, observed, received, size, rest = rest.split(b"\n", 4)
            assert clock <= int(sent) <= int(observed) <= int(received)
            timings[side] = [sent, observed, received, size]
            clock = int(received)
            n = int(size)
            assert rest[:n] == (installed / ("body-" + side)).read_bytes()
            (installed / ("body-" + side)).unlink()
            rest = rest[n:]
        length, report = rest.split(b"\n", 1)
        assert len(report) == int(length) and report.decode() == saved.stdout
        assert f"Capture UTC {clock} (ages at capture)" in saved.stdout
        assert "180 / 60" in saved.stdout and "210 / 60" in saved.stdout
        if os.name != "nt":
            assert archive.stat().st_mode & 0o777 == 0o600

        refused = run(*save)
        assert refused.returncode == 3 and not refused.stdout and "existing archive was preserved" in refused.stderr
        launched(["4", "5"])
        assert archive.read_bytes() == data and not temporary.exists()
        scenario.unlink()

        changed_price = body.replace(b'"2","2","2","2","1"', b'"3","3","3","3","1"', 1)
        assert changed_price != body

        def change_receipt(side, field, value):
            original = timings[side]
            changed = [*original]
            changed[field] = str(value).encode("ascii")
            return signed(body.replace(b"\n".join(original) + b"\n", b"\n".join(changed) + b"\n", 1))

        for altered, problem in (
            (data[:-1], "review archive unavailable or invalid"),
            (signed(changed_price), "report differs from the recalculated report"),
            (signed(body.replace(b"\n" + saved_end + b"\n", b"\n" + str(end + 60).encode() + b"\n", 1)),
             "review archive unavailable or invalid"),
            (change_receipt("pearl", 1, int(timings["pearl"][0]) - 1), "saved public hour response unavailable or invalid"),
            (change_receipt("pearl", 2, int(timings["fx"][0]) + 1), "review archive unavailable or invalid"),
            (change_receipt("fx", 1, clock + 1), "saved public hour response unavailable or invalid"),
            (change_receipt("fx", 2, clock + 3600), "saved review cutoff is in the future"),
        ):
            archive.write_bytes(altered)
            invalid = run("--open-review", archive.name)
            assert invalid.returncode == 3 and not invalid.stdout and problem in invalid.stderr, invalid
            assert not requests.exists()
        archive.write_bytes(data)

        for mode, expected in (("gap-pearl", "59/60"), ("malformed", "invalid minute response schema"),
                               ("future", "invalid response observation time"), ("limited", "rate limited"),
                               ("blocked", "non-public destination refused")):
            scenario.write_text(mode, encoding="ascii")
            result = run(allow, fallback)
            assert result.returncode == 3 and expected in result.stdout and "Kraken" not in result.stdout, result
            launched(["4", "5"])
        for mode, expected in (("kraken-gap", "59/60"), ("kraken-open-in-window", "invalid minute response schema"),
                               ("kraken-disordered", "invalid minute response schema"),
                               ("kraken-limited-json", "provider rate limited"),
                               ("kraken-throttled", "provider throttled the request")):
            scenario.write_text(mode, encoding="ascii")
            result = run(allow, fallback)
            assert result.returncode == 3 and f"Kraken USDT/USD fallback: {expected}" in result.stdout, result
            assert "180 / 60" in result.stdout and "Mean USD per PRL (exact sum / minutes): N/A" in result.stdout
            launched(["4", "5", "3"])

        fallback_archive = operator / "fallback.cmh"
        scenario.write_text("kraken-archive", encoding="ascii")
        captured = run(allow, *dns, fallback, "--save-review", fallback_archive.name)
        assert captured.returncode == 0 and not captured.stderr, captured
        assert "Coinbase USDT/USD: 59/60" in captured.stdout
        assert "Kraken USDT/USD fallback: 60/60" in captured.stdout
        assert "FX source for USD mean: Kraken USDT/USD (candidate, minute starts inferred at receipt)" in captured.stdout
        assert "180 / 60" in captured.stdout and "480 / 60" in captured.stdout
        _, fallback_end = launched(["4", "5", "3"])
        assert resolver_requests.read_text(encoding="ascii").splitlines() == [resolver] * 3
        resolver_requests.unlink()
        fallback_data = fallback_archive.read_bytes()
        assert fallback_data.startswith(b"CANARY-MARKET-HOUR-REVIEW-3\n")
        assert resolver.encode("ascii") not in fallback_data
        fallback_body = fallback_data[:-65]
        details = fallback_body.split(b"\n", 3)[-1]
        receipts = []
        for side in ("pearl", "fx", "kraken"):
            sent, observed, received, size, details = details.split(b"\n", 4)
            payload_size = int(size)
            payload, details = details[:payload_size], details[payload_size:]
            side_path = installed / ("body-" + side)
            assert payload == side_path.read_bytes()
            receipts.append((sent, observed, received, payload))
            side_path.unlink()
        assert int(receipts[1][2]) <= int(receipts[2][0])
        assert details.split(b"\n", 1)[1].decode() == captured.stdout
        kraken_payload = receipts[2][-1]
        coinbase_payload = receipts[1][-1]
        coinbase_full = json.loads(coinbase_payload)
        coinbase_full["candles"].append(dict(start=str(fallback_end - 60), open="1.5", high="1.5",
                                             low="1.5", close="1.5", volume="1"))
        for payload, forged in ((kraken_payload, kraken_payload.replace(b"USDTZUSD", b"USDCZUSD")),
                                (kraken_payload, kraken_payload.replace(b'"error":[]', b'"error":["fixture failure"]')),
                                (coinbase_payload, json.dumps(coinbase_full).encode("ascii"))):
            prefix = str(len(payload)).encode("ascii") + b"\n" + payload
            revised = str(len(forged)).encode("ascii") + b"\n" + forged
            assert prefix in fallback_body and forged != payload
            fallback_archive.write_bytes(signed(fallback_body.replace(prefix, revised, 1)))
            refused = run("--open-review", fallback_archive.name)
            assert refused.returncode == 3 and not refused.stdout and "saved Kraken fallback" in refused.stderr, refused
            assert not requests.exists()
        fallback_archive.write_bytes(fallback_data)
        scenario.unlink()

        for mode, flags, expected, feeds, code in (
            ("gap-pearl", (), "MEXC Pearl mainnet PRL/USDT missing elapsed-minute ranges", ["4", "5"], 3),
            ("limited", (), "HTTP status 429 (Retry-After 1 s)", ["4", "5"], 3),
            ("forbidden", (), "HTTP status 403", ["4", "5"], 3),
            ("restricted", (), "HTTP status 451", ["4", "5"], 3),
            ("server-error", (), "HTTP status 503", ["4", "5"], 3),
            ("blocked", (), "non-public destination refused", ["4", "5"], 3),
            ("malformed", (fallback,), "invalid minute response schema", ["4", "5"], 3),
            ("future", (fallback,), "invalid response observation time", ["4", "5"], 3),
            ("kraken-gap", (fallback,), "Kraken USDT/USD fallback missing elapsed-minute ranges", ["4", "5", "3"], 3),
            ("kraken-throttled", (fallback,), "provider throttled the request", ["4", "5", "3"], 3),
            ("kraken-archive", (fallback,), "480 / 60", ["4", "5", "3"], 0),
        ):
            scenario.write_text(mode, encoding="ascii")
            sample = run(*capture, *flags, *dns)
            assert sample.returncode == code and not sample.stderr and expected in sample.stdout, sample
            assert ("Sample result: incomplete" in sample.stdout) == (code == 3), sample
            sample_start, sample_end = launched(feeds)
            if mode == "gap-pearl":
                assert f"[{sample_end - 60}, {sample_end})" in sample.stdout
            if mode == "kraken-gap":
                assert f"[{sample_start + 12 * 60}, {sample_start + 13 * 60})" in sample.stdout
                assert f"[{sample_end - 60}, {sample_end})" in sample.stdout
            if mode in ("limited", "forbidden", "restricted", "server-error", "blocked", "malformed", "future"):
                assert "missing elapsed-minute ranges" not in sample.stdout, sample
            assert resolver_requests.read_text(encoding="ascii").splitlines() == [resolver] * len(feeds)
            resolver_requests.unlink()
            captured_bytes = sample_archive.read_bytes()
            captured_body = captured_bytes[:-65]
            assert captured_bytes == signed(captured_body)
            assert captured_body.startswith(b"CANARY-MARKET-HOUR-REVIEW-4\n")
            assert resolver.encode("ascii") not in captured_body
            if mode == "limited":
                assert b"CMJSON1 10 429 0 1 0\n" in captured_body
            if mode == "malformed":
                assert b'{"private-sentinel":"untrusted"}' in captured_body
            if mode == "kraken-archive":
                for role in ("pearl", "fx", "kraken"):
                    original_body = installed / ("body-" + role)
                    assert original_body.read_bytes() in captured_body
                    original_body.unlink()

            replay_sample = run("--open-review", sample_archive.name)
            assert replay_sample.returncode == 0 and replay_sample.stdout == sample.stdout and not replay_sample.stderr
            assert not requests.exists() and captured_bytes == sample_archive.read_bytes()

            if mode == "kraken-gap":
                header, initial, closing, permission, count, receipts = captured_body.split(b"\n", 5)
                assert permission == b"2" and count == b"3"
                changed = b"\n".join((header, initial, closing, b"1", count, receipts))
                sample_archive.write_bytes(signed(changed))
                refused_sample = run("--open-review", sample_archive.name)
                assert refused_sample.returncode == 3 and not refused_sample.stdout
                assert "captured request permission" in refused_sample.stderr

            if mode == "limited":
                sample_archive.write_bytes(signed(captured_body.replace(b"rate limited", b"rate altered", 1)))
                refused_sample = run("--open-review", sample_archive.name)
                assert refused_sample.returncode == 3 and not refused_sample.stdout
                assert "differs from the recalculated report" in refused_sample.stderr
                sample_archive.write_bytes(captured_bytes)
                refused_sample = run(*capture)
                assert refused_sample.returncode == 3 and not refused_sample.stdout
                assert "existing archive was preserved" in refused_sample.stderr
                launched(feeds)
                assert captured_bytes == sample_archive.read_bytes()

            assert not sample_archive.with_suffix(".cmh.tmp").exists()
            sample_archive.unlink()
        scenario.write_text("cancelled", encoding="ascii")
        cancelled_sample = run(*capture)
        assert cancelled_sample.returncode == 130 and not cancelled_sample.stdout
        assert "sample cancelled" in cancelled_sample.stderr
        launched(["4", "5"])
        assert not sample_archive.exists() and not sample_archive.with_suffix(".cmh.tmp").exists()
        scenario.unlink()

        cancellations = (("slow", (), ("4",), "--save-review"),
                         ("slow-kraken", (fallback,), ("4", "5", "3"), "--save-review"),
                         ("slow-kraken", (fallback,), ("4", "5", "3"), "--save-sample"))
        for mode, flags, feeds, save_flag in cancellations if os.name != "nt" else ():
            scenario.write_text(mode, encoding="ascii")
            with subprocess.Popen([cli, "market-hour", allow, *flags, save_flag, "cancelled.cmh"],
                                  cwd=operator, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True) as child:
                try:
                    deadline = time.monotonic() + 4
                    while time.monotonic() < deadline:
                        if requests.exists() and any(line.startswith(feeds[-1] + " ")
                                                     for line in requests.read_text().splitlines()):
                            break
                        assert child.poll() is None
                        time.sleep(0.02)
                    else:
                        raise AssertionError(f"expected helper {feeds[-1]} did not launch")
                    child.send_signal(signal.SIGINT)
                    out, error = child.communicate(timeout=4)
                    assert child.returncode == 130 and not out and "sample cancelled" in error
                finally:
                    if child.poll() is None:
                        child.kill()
                        child.wait(timeout=3)
            launched(list(feeds))
            assert not (operator / "cancelled.cmh").exists() and not (operator / "cancelled.cmh.tmp").exists()

        helper.unlink()
        reopened = run("--open-review", archive.name)
        assert reopened.returncode == 0 and reopened.stdout == saved.stdout and not reopened.stderr, reopened
        assert not requests.exists() and archive.read_bytes() == data
        archive.unlink()
        reopened = run("--open-review", fallback_archive.name)
        assert reopened.returncode == 0 and reopened.stdout == captured.stdout and not reopened.stderr, reopened
        assert not requests.exists() and fallback_archive.read_bytes() == fallback_data
        fallback_archive.unlink()
        result = run(allow)
        assert result.returncode == 3 and unavailable in result.stdout and "180 / 60" not in result.stdout
        assert not requests.exists() and not (operator / requests.name).exists()
        assert {p.name for p in operator.iterdir()} == {"config.json", worker_name}
        assert config.read_text() == "private-sentinel"
    print("Public hour: bounded requests, independent FX fallback, exact means, archive replay, error/cancellation and consent")
    return 0


if __name__ == "__main__":
    sys.exit(main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), sys.argv[3] == "1", "--sample-only" in sys.argv[4:]))
