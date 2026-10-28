"""Native terminal projection only. The preview tool cannot fetch prices or start a miner."""
import argparse
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("preview")
    args = parser.parse_args()
    cases = 0
    for mode in ("fresh", "stale", "missing-fx", "clock-fault"):
        for width in (60, 80, 128):
            result = subprocess.run([args.preview, mode, str(width)], capture_output=True, encoding="utf8", timeout=5)
            assert result.returncode == 0, result.stderr
            lines = result.stdout.splitlines()
            assert lines and max(map(len, lines)) <= width
            text = " ".join(" ".join(lines).split())
            assert "PUBLIC MARKET REFERENCES" in text
            assert "SYNTHETIC FIXTURE - NOT LIVE" in text
            assert "trade-event time NOT SUPPLIED" in text
            assert "Actual credited" not in text and "Estimated net" not in text
            if mode == "fresh":
                assert "Latest PRL/USD reference: 2.45" in text
                assert "2.000000 USDT/PRL | 5.000000 USD/PRL" in text
                assert "Pearl 60/60 | FX 60/60" in text
            elif mode == "missing-fx":
                assert "2.000000 USDT/PRL | N/A USD/PRL" in text
                assert "Latest PRL/USD reference: N/A" in text
            elif mode == "stale":
                assert "stale/invalid cached reference 2.5" in text
                assert "Latest PRL/USD reference: N/A" in text
            else:
                assert "clock fault" in text and "Latest PRL/USD reference: N/A" in text
            cases += 1
    # Existing terminal renderer clamps requests below 60 columns. Do not claim this fits a 40-column terminal.
    narrow = subprocess.run([args.preview, "fresh", "40"], capture_output=True, encoding="utf8", timeout=5)
    assert narrow.returncode == 0 and max(map(len, narrow.stdout.splitlines())) <= 60
    print(f"native market dashboard: {cases} mode/width scenarios passed; synthetic input only")


if __name__ == "__main__":
    main()
