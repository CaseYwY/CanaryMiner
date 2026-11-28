"""Verify original one-shot admission with the real process client and an inert installed worker."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

from test_market_reward_cli import PUBLIC_HEIGHT, TIP_HASH, status


def main(cli, worker):
    suffix = ".exe" if os.name == "nt" else ""
    failures = []
    with tempfile.TemporaryDirectory(prefix="canary-sample-epoch-") as path:
        root = Path(path)
        fixture = shutil.copy2(cli, root / ("sample" + suffix))
        shutil.copy2(worker, root / ("canary_public_json_worker" + suffix))
        cases = (
            ("market-tickers", ["--allow-public-tickers"], "ticker-requests", ["0", "2"],
             "Latest PRL/USD reference: 2.45"),
            ("market-tickers", ["--allow-public-tickers", "--save-review", "capture.cmt"], "ticker-requests", ["0", "2"],
             "PRL/USD reference at capture: 2.45"),
            ("market-tickers", ["--allow-public-tickers", "--save-sample", "capture.cmt"], "ticker-requests", ["0", "2"],
             "PRL/USD reference at capture: 2.45"),
            ("market-hour", ["--allow-public-candles"], "candle-requests", ["4", "5"],
             "Mean USD per PRL (exact sum / minutes): 210 / 60"),
            ("market-hour", ["--allow-public-candles", "--save-review", "capture.cmh"], "candle-requests", ["4", "5"],
             "Mean USD per PRL (exact sum / minutes): 210 / 60"),
            ("market-hour", ["--allow-public-candles", "--kraken-fallback", "--save-review", "fallback.cmh"],
             "candle-requests", ["4", "5", "3"], "Mean USD per PRL (exact sum / minutes): 480 / 60"),
        )
        (root / "reward-response.json").write_text(json.dumps(status(PUBLIC_HEIGHT, int(time.time()) - 1)), encoding="ascii")
        (root / "reward-indexed.json").write_text(json.dumps({"blockHash": TIP_HASH}), encoding="ascii")
        (root / "sample-lifetime-mode").write_text("identity-pacing", encoding="ascii")
        (root / "identity-scenario").write_text("pacing", encoding="ascii")
        paced = subprocess.run([fixture, "market-tickers", "--allow-public-tickers"], cwd=root,
                               capture_output=True, text=True, timeout=8)
        if paced.returncode == 77:
            return 77
        assert paced.returncode == 0 and not paced.stdout and not paced.stderr, paced
        attempts = [line.split() for line in (root / "market-attempts").read_text().splitlines()]
        assert [feed for feed, _ in attempts] == ["12", "0", "12", "0"], attempts
        times = [int(stamp) for _, stamp in attempts]
        assert all(b - a >= 1000 for a, b in zip(times, times[1:])), times
        (root / "identity-scenario").unlink()
        (root / "identity-requests").unlink()
        (root / "ticker-requests").unlink()
        (root / "sample-lifetime-mode").write_text("identity-pacing-deadline", encoding="ascii")
        expired = subprocess.run([fixture, "market-tickers", "--allow-public-tickers"], cwd=root,
                                  capture_output=True, text=True, timeout=4)
        assert expired.returncode == 0 and not expired.stdout and not expired.stderr, expired
        assert (root / "identity-requests").read_text().splitlines() == ["12"]
        assert not (root / "ticker-requests").exists()
        cases += (("market-reward", ["--allow-public-reward", "--save-review", "capture.cmr"],
                   "reward-requests", ["6", "9", "6"], "tip verified across three requests"),)
        for command, args, log, feeds, expected_report in cases:
            requests = root / log
            saving = "--save-review" in args or "--save-sample" in args
            archive = root / (args[-1] if saving else "unexpected.cmt")
            modes = ("fresh", "stale", "first", "second") if sys.platform.startswith("linux") else ("fresh", "stale")
            if "--kraken-fallback" in args:
                (root / "candle-scenario").write_text("gap-fx", encoding="ascii")
            if len(feeds) == 3 and sys.platform.startswith("linux"):
                modes += ("third",)
            if command == "market-reward" and sys.platform.startswith("linux"):
                modes += ("deadline-third",)
            for mode in modes:
                requests.unlink(missing_ok=True)
                archive.unlink(missing_ok=True)
                (root / "sample-lifetime-mode").write_text(mode, encoding="ascii")
                result = subprocess.run([fixture, command, *args], cwd=root, capture_output=True,
                                        text=True, timeout=8)
                if result.returncode == 77 and mode == "fresh":
                    return 77
                launched = [line.split()[0] for line in requests.read_text().splitlines()] if requests.exists() else []
                if mode == "fresh":
                    valid = result.returncode == 0 and expected_report in result.stdout and launched == feeds
                elif mode == "deadline-third":
                    valid = (result.returncode == 3 and not result.stdout and launched == feeds[:2]
                             and "fixture deadline expired" in result.stderr and "deadline exceeded" in result.stderr)
                else:
                    launches = {"second": 1, "third": 2}.get(mode, 0)
                    expected = feeds[:launches]
                    valid = (result.returncode == 3 and not result.stdout and launched == expected
                             and "helper ownership uncertain" in result.stderr)
                    if mode in ("first", "second", "third"):
                        valid = valid and "fixture epoch advanced" in result.stderr
                if not valid:
                    failures.append((command, mode, launched, result.returncode, result.stdout, result.stderr))
                assert archive.exists() == (saving and mode == "fresh"), result
                assert not archive.with_name(archive.name + ".tmp").exists(), result
            if "--kraken-fallback" in args:
                (root / "candle-scenario").unlink()
        if sys.platform.startswith("linux"):
            (root / "sample-lifetime-mode").write_text("identity-between", encoding="ascii")
            (root / "ticker-requests").unlink(missing_ok=True)
            (root / "identity-requests").unlink(missing_ok=True)
            split = subprocess.run([fixture, "market-tickers", "--allow-public-tickers"], cwd=root,
                                   capture_output=True, text=True, timeout=8)
            assert split.returncode == 0 and not split.stdout and not split.stderr, split
            assert (root / "identity-requests").read_text().splitlines() == ["12"]
            assert not (root / "ticker-requests").exists()
            (root / "sample-lifetime-mode").write_text("clock-rollback", encoding="ascii")
            (root / "ticker-scenario").write_text("clock-rollback", encoding="ascii")
            requests = root / "ticker-requests"
            requests.unlink(missing_ok=True)
            result = subprocess.run([fixture, "market-tickers", "--allow-public-tickers", "--save-sample", "rollback.cmt"],
                                    cwd=root, capture_output=True, text=True, timeout=8)
            assert result.returncode == 3 and not result.stdout and "local clock unavailable or changed" in result.stderr, result
            assert requests.read_text().splitlines() == ["0"]
            assert not (root / "rollback.cmt").exists() and not (root / "rollback.cmt.tmp").exists()
            requests.unlink()
            (root / "ticker-scenario").unlink()
            (root / "sample-lifetime-mode").write_text("stop-after-sync", encoding="ascii")
            stopped = subprocess.run([fixture, "market-tickers", "--allow-public-tickers", "--save-sample", "sync.cmt"],
                                     cwd=root, capture_output=True, text=True, timeout=8)
            assert stopped.returncode == 130 and not stopped.stdout and "sample cancelled" in stopped.stderr, stopped
            assert requests.read_text().splitlines() == ["0", "2"]
            assert not (root / "sync.cmt").exists() and not (root / "sync.cmt.tmp").exists()
    for failure in failures:
        print(repr(failure))
    if failures:
        return 1
    print("One-shot commands preserve original admission before and between native helper launches.")
    return 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
