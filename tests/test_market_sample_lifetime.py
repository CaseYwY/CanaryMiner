"""Verify original one-shot admission with the real process client and an inert installed worker."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


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
            ("market-hour", ["--allow-public-candles"], "candle-requests", ["4", "5"],
             "Mean USD per PRL (exact sum / minutes): 210 / 60"),
            ("market-hour", ["--allow-public-candles", "--save-review", "capture.cmh"], "candle-requests", ["4", "5"],
             "Mean USD per PRL (exact sum / minutes): 210 / 60"),
        )
        for command, args, log, feeds, expected_report in cases:
            requests = root / log
            archive = root / (args[-1] if "--save-review" in args else "unexpected.cmt")
            modes = ("fresh", "stale", "first", "second") if sys.platform.startswith("linux") else ("fresh", "stale")
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
                else:
                    expected = feeds[:1] if mode == "second" else []
                    valid = (result.returncode == 3 and not result.stdout and launched == expected
                             and "helper ownership uncertain" in result.stderr)
                    if mode in ("first", "second"):
                        valid = valid and "fixture epoch advanced" in result.stderr
                if not valid:
                    failures.append((command, mode, launched, result.returncode, result.stdout, result.stderr))
                assert archive.exists() == ("--save-review" in args and mode == "fresh"), result
        if sys.platform.startswith("linux"):
            (root / "sample-lifetime-mode").write_text("clock-rollback", encoding="ascii")
            (root / "ticker-scenario").write_text("clock-rollback", encoding="ascii")
            requests = root / "ticker-requests"
            requests.unlink(missing_ok=True)
            result = subprocess.run([fixture, "market-tickers", "--allow-public-tickers", "--save-review", "rollback.cmt"],
                                    cwd=root, capture_output=True, text=True, timeout=8)
            assert result.returncode == 3 and not result.stdout and "local clock unavailable or changed" in result.stderr, result
            assert requests.read_text().splitlines() == ["0"]
            assert not (root / "rollback.cmt").exists() and not (root / "rollback.cmt.tmp").exists()
            requests.unlink()
            (root / "ticker-scenario").unlink()
            (root / "sample-lifetime-mode").write_text("stop-after-sync", encoding="ascii")
            stopped = subprocess.run([fixture, "market-tickers", "--allow-public-tickers", "--save-review", "sync.cmt"],
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
