"""One-shot hour sample using only an inert installed sibling."""
import hashlib
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
    allow = "--allow-public-candles"
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
        requests = installed / "candle-requests"
        scenario = installed / "candle-scenario"
        archive = operator / "public.cmh"
        save = (allow, "--save-review", archive.name)
        temporary = archive.with_suffix(".cmh.tmp")

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
            assert result.returncode == 0 and "market-hour " + allow in result.stdout
        for arguments in ((), (allow, allow), (allow, "--no-market"), (allow, "--config", config.name),
                          ("--file", config.name, allow), (allow + "=true",), ("--help", allow),
                          ("--allow-public-candle\uff53",), ("--url", "private-sentinel")):
            result = run(*arguments)
            assert result.returncode == 2 and not result.stdout and not requests.exists(), result
        result = call("market-hou\uff52", allow)
        assert result.returncode == 2 and not result.stdout and not requests.exists(), result

        ticker = "--allow-public-tickers"
        dashboard_refusals = (
            ([allow], "Dashboard candles require --allow-public-tickers as well"),
            ([ticker, allow, allow], "--allow-public-candles is accepted once, for run only"),
            ([ticker, allow + "=true"], "--allow-public-candles takes no value"),
            (["--pass", ticker, allow], "Dashboard candles require --allow-public-tickers as well"),
        )
        # Native installs only exercise the one-shot command; run refusals use the inert host binary.
        for args, expected in (() if sample_only else dashboard_refusals):
            result = subprocess.run([cli, "run", "--pool", "kryptex", "--wallet", "prl1" + "q" * 59,
                                     *args], cwd=installed, capture_output=True, text=True, timeout=8)
            assert result.returncode == 2 and not result.stdout and expected in result.stderr, result
            assert not requests.exists() and not (installed / "ticker-requests").exists()

        if not enabled:
            for args in ((allow,), save):
                result = run(*args)
                assert result.returncode == 3 and not result.stdout and "HTTPS unavailable in this build" in result.stderr
            assert not requests.exists()
            assert not archive.exists()
            print("Public hour: default build refuses network opt-in")
            return 0
        result = run(allow)
        unavailable = "verified HTTPS/c-ares backend unavailable"
        if sys.platform == "linux" and not requests.exists() and unavailable in result.stdout:
            libc = os.confstr("CS_GNU_LIBC_VERSION")
            assert libc and tuple(map(int, libc.split()[1].split(".")[:2])) < (2, 40), result
            return 77
        assert result.returncode == 0 and not result.stderr, result
        start, end = launched(["4", "5"])
        assert f"UTC seconds [{start}, {end})" in result.stdout
        assert re.search(r"window ended [0-9]+ s ago", result.stdout)
        assert "candle finality is unverified" in result.stdout
        assert result.stdout.count("60/60 elapsed minute buckets") == 2
        assert "180 / 60" in result.stdout and "210 / 60" in result.stdout
        assert len(re.findall(r"observed UTC [0-9]+ \(receipt age [0-9]+ s\)", result.stdout)) == 2

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
        saved = run(*save)
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

        if os.name != "nt":
            scenario.write_text("slow", encoding="ascii")
            with subprocess.Popen([cli, "market-hour", allow, "--save-review", "cancelled.cmh"],
                                  cwd=operator, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True) as child:
                try:
                    deadline = time.monotonic() + 4
                    while time.monotonic() < deadline:
                        if requests.exists() and requests.read_text().startswith("4 "):
                            break
                        assert child.poll() is None
                        time.sleep(0.02)
                    else:
                        raise AssertionError("first helper did not launch")
                    child.send_signal(signal.SIGINT)
                    out, error = child.communicate(timeout=4)
                    assert child.returncode == 130 and not out and "sample cancelled" in error
                finally:
                    if child.poll() is None:
                        child.kill()
                        child.wait(timeout=3)
            launched(["4"])
            assert not (operator / "cancelled.cmh").exists() and not (operator / "cancelled.cmh.tmp").exists()

        helper.unlink()
        reopened = run("--open-review", archive.name)
        assert reopened.returncode == 0 and reopened.stdout == saved.stdout and not reopened.stderr, reopened
        assert not requests.exists() and archive.read_bytes() == data
        archive.unlink()
        result = run(allow)
        assert result.returncode == 3 and unavailable in result.stdout and "180 / 60" not in result.stdout
        assert not requests.exists() and not (operator / requests.name).exists()
        assert {p.name for p in operator.iterdir()} == {"config.json", worker_name}
        assert config.read_text() == "private-sentinel"
    print("Public hour: bounded requests, exact means, archive raw bytes and replay, error/cancellation and consent")
    return 0


if __name__ == "__main__":
    sys.exit(main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), sys.argv[3] == "1", "--sample-only" in sys.argv[4:]))
