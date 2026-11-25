"""One-shot hour sample using only an inert installed sibling."""
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time


def main(cli_path, fixture, enabled):
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

        if not enabled:
            result = run(allow)
            assert result.returncode == 3 and not result.stdout and "HTTPS unavailable in this build" in result.stderr
            assert not requests.exists()
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
        ):
            scenario.write_text(mode, encoding="ascii")
            result = run(allow)
            assert result.returncode == 3 and not result.stderr and expected in result.stdout, result
            assert "210 / 60" not in result.stdout and "Mean USD per PRL (exact sum / minutes): N/A" in result.stdout
            assert ("180 / 60" in result.stdout) == (mode != "gap-pearl")
            launched(["4", "5"])

        if os.name != "nt":
            scenario.write_text("slow", encoding="ascii")
            with subprocess.Popen([cli, "market-hour", allow], cwd=operator, stdout=subprocess.PIPE,
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

        helper.unlink()
        result = run(allow)
        assert result.returncode == 3 and unavailable in result.stdout and "180 / 60" not in result.stdout
        assert not requests.exists() and not (operator / requests.name).exists()
        assert {p.name for p in operator.iterdir()} == {"config.json", worker_name}
        assert config.read_text() == "private-sentinel"
    print("Public hour: two bounded requests, exact aligned means, partial coverage, errors, consent and cancellation")
    return 0


if __name__ == "__main__":
    sys.exit(main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), sys.argv[3] == "1"))
