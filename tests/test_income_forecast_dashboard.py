"""Exact forecast display through the existing terminal dashboard, no production activation."""
import re
import subprocess
import sys


def main():
    checks = 0
    for mode in ('complete', 'partial', 'missing-fx', 'no-electricity', 'missing-work', 'stale'):
        for width in (60, 80, 120):
            result = subprocess.run([sys.argv[1], mode, str(width)], capture_output=True, text=True, timeout=5)
            assert result.returncode == 0
            text = result.stdout
            assert 'SYNTHETIC' in text and 'PRODUCTION AND REFERENCE' in text
            assert 'fixture.private' not in text and 'synthetic-worker-A' not in text and 'fixture.local-measured' not in text
            assert all(len(line) <= width for line in text.splitlines())
            joined = re.sub(r'\s+', ' ', text)
            assert 'Actual credited 24h: 23.00000000 PRL' in joined
            if mode == 'no-electricity':
                assert 'Estimated net today' not in text
            else:
                assert 'Estimated net today' in text
            if mode in ('partial', 'missing-work'):
                assert 'Today forecast: N/A PRL' in joined
            else:
                assert 'Today forecast: 33.00000000 PRL' in joined
            if mode in ('missing-fx', 'stale', 'partial', 'missing-work'):
                assert 'Estimated net today: N/A USD' in joined
            if mode == 'complete':
                assert 'Estimated net today: 80.25 USD' in joined
            checks += 1
    # The established renderer clamps requests below 60 columns; this is not 40-column layout support.
    narrow = subprocess.run([sys.argv[1], 'complete', '40'], capture_output=True, text=True, timeout=5)
    assert narrow.returncode == 0 and max(map(len, narrow.stdout.splitlines())) <= 60
    print(f'income forecast dashboard: {checks} mode/width scenarios passed')


if __name__ == '__main__':
    main()
