"""Actual process crash/WAL rollback and synthetic database corruption. Never opens a user's ledger."""
import argparse
import hashlib
import os
from pathlib import Path
import sqlite3
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe")
    args = parser.parse_args()
    probe = str(Path(args.probe).resolve(strict=True))
    checks = 0
    def run(file, action, expected=0):
        nonlocal checks
        result = subprocess.run([probe, str(file), action], capture_output=True, encoding="utf8", timeout=10)
        assert result.returncode == expected, (action, result.returncode, result.stdout, result.stderr)
        checks += 1
        return result.stdout
    with tempfile.TemporaryDirectory(prefix="canary-credit-recovery-", dir=os.environ.get("CANARY_TEST_TMP")) as temp:
        root = Path(temp)
        file = root / "synthetic.db"
        assert "quantity=4.25 complete=1" in run(file,"init")
        assert "quantity=4.25 complete=1" in run(file,"replay")
        assert "observed=1800000000000" in run(file,"query")
        assert "quantity=4.25 complete=1" in run(file,"rollback")
        run(file,"crash",17)
        assert "quantity=4.25 complete=1" in run(file,"query")
        assert "quantity=3.75 complete=1" in run(file,"correct")
        assert "quantity=3.75 complete=1" in run(file,"query")
        with sqlite3.connect(file) as provenance:
            assert provenance.execute("SELECT revision,first_sequence FROM versions ORDER BY revision").fetchall() == [(1,1),(2,2)]
            assert provenance.execute("SELECT source_asof,observed FROM snapshots ORDER BY sequence").fetchall() == [(1800000000000,1800000000000),(1800000001000,1800000001000)]
        provenance.close()
        checks += 2
        busy = root / "busy.db"
        run(busy,"init")
        database = sqlite3.connect(busy)
        database.execute("BEGIN IMMEDIATE")
        start = time.monotonic()
        run(busy,"correct",1)
        assert time.monotonic() - start < 3, "SQLite writer contention blocked despite zero busy timeout"
        database.rollback()
        database.close()
        assert "quantity=4.25 complete=1" in run(busy,"query")
        corruptions = {
            "future-version": "PRAGMA user_version=2",
            "wrong-application": "PRAGMA application_id=1",
            "invalid-quantity": "UPDATE records SET amount='NaN'",
            "audit-mismatch": "UPDATE versions SET amount='3'",
            "coverage-mismatch": "UPDATE coverage SET end_ms=end_ms-1",
            "snapshot-mismatch": "UPDATE meta SET last_batch='[]'",
            "missing-provenance": "UPDATE versions SET first_sequence=999",
            "unexpected-table": "CREATE TABLE payouts(value TEXT)",
            "unexpected-trigger": "CREATE TRIGGER surprise AFTER INSERT ON records BEGIN DELETE FROM coverage; END",
        }
        for name, sql in corruptions.items():
            bad = root / f"{name}.db"
            run(bad,"init")
            with sqlite3.connect(bad) as database:
                database.execute(sql)
            database.close()
            before = hashlib.sha256(bad.read_bytes()).digest()
            run(bad,"query",1)
            assert hashlib.sha256(bad.read_bytes()).digest() == before, "rejected database was changed"
            checks += 1
        print(f"credit recovery: {checks} process/integrity checks passed; private synthetic files only")


if __name__ == "__main__":
    main()
