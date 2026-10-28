"""Ordinary release-path credit targets must not contain the private stall/crash test seams."""
import pathlib
import sys

def main():
    for name in sys.argv[1:]:
        data = pathlib.Path(name).read_bytes()
        for marker in (b"fixture.stall-after-apply", b"fixture.stall-before-apply", b"fixture.stall-prepared", b"fixture.stall-unsettled", b"fixture-stall-open.db", b"fixture.bad-ack", b"CreditLedgerTestPeer"):
            assert marker not in data, "Private credit fixture seam present in ordinary target"
    print("ordinary credit helper/library: no private stall/crash fixture seams")


if __name__ == "__main__":
    main()
