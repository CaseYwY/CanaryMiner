"""Python cross-check of the self-contained gzip encoder (H-019 task 4).

Usage: test_pearl_gzip.py <test_pearl_gzip binary> <scratch dir>

The binary writes the gzip stream it produced and the raw bytes it was given.
This file checks that the standard library decodes them, that zlib agrees, and
that the output is a single well-formed member with the expected trailer.
"""
import gzip
import os
import struct
import subprocess
import sys
import zlib


def main():
    if len(sys.argv) != 3:
        print("usage: test_pearl_gzip.py <test_pearl_gzip> <scratch dir>")
        return 2
    binary, scratch = sys.argv[1], sys.argv[2]
    os.makedirs(scratch, exist_ok=True)
    gz_path = os.path.join(scratch, "encoder.gz")
    raw_path = os.path.join(scratch, "encoder.raw")
    subprocess.run([binary, gz_path, raw_path], check=True, stdout=subprocess.DEVNULL)

    with open(gz_path, "rb") as f:
        stream = f.read()
    with open(raw_path, "rb") as f:
        raw = f.read()

    failures = []

    def check(condition, name):
        print("  %-68s %s" % (name, "ok" if condition else "FAIL"))
        if not condition:
            failures.append(name)

    check(stream[:10] == b"\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\xff", "fixed 10-byte gzip header")
    check(gzip.decompress(stream) == raw, "gzip module decodes the encoder output to the raw input")
    check(zlib.decompress(stream, 31) == raw, "zlib (wbits 31) decodes the same stream")
    isize = struct.unpack("<I", stream[-4:])[0]
    check(isize == len(raw) % (1 << 32), "ISIZE trailer equals the raw length")
    crc = struct.unpack("<I", stream[-8:-4])[0]
    check(crc == zlib.crc32(raw), "CRC-32 trailer equals zlib.crc32 of the raw input")
    check(len(stream) * 10 < len(raw), "repetitive proof-like input compresses by more than 10x")
    member = zlib.decompressobj(31)
    member_out = member.decompress(stream)
    check(member.eof and member.unused_data == b"" and member_out == raw,
          "exactly one gzip member, with no trailing bytes")

    if failures:
        print("python gzip cross-check: FAILED (%d)" % len(failures))
        return 1
    print("python gzip cross-check: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
