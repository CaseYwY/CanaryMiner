"""Prove the genuine worker's DNS option in a loopback-only Linux namespace."""
import errno
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import time


HOST = "api.gateio.ws"
DESTINATION_BLOCKED = b"CMJSON1 6 0 0 0 0\n"
TIMED_OUT = b"CMJSON1 5 0 0 0 0\n"


def read_question(packet):
    pos = 12
    labels = []
    while packet[pos]:
        start = pos + 1
        pos = start + packet[pos]
        labels.append(packet[start:pos].decode("ascii").lower())
    end = pos + 5
    qtype, qclass = struct.unpack("!HH", packet[pos + 1:end])
    assert qclass == 1 and qtype in (1, 28), (qclass, qtype)
    return ".".join(labels), qtype, packet[12:end]


def sample(worker, family, resolver, expected, answer=True):
    try:
        server = socket.socket(family, socket.SOCK_DGRAM)
    except OSError as error:
        if error.errno == errno.EAFNOSUPPORT:
            return False
        raise
    with server:
        try:
            server.bind((resolver, 53))
        except OSError as error:
            if error.errno in (errno.EACCES, errno.EADDRINUSE, errno.EADDRNOTAVAIL):
                return False
            raise
        server.settimeout(0.05)
        # Simulate a deployment with a local search suffix and a high ndots setting.
        env = {**os.environ, "LOCALDOMAIN": "fixture-private.invalid", "RES_OPTIONS": "ndots:15"}
        args = [worker, "0", "0", "0", "750", "1250", "65536", "32768", resolver]
        queries = []
        with subprocess.Popen(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as child:
            try:
                deadline = time.monotonic() + 6
                while child.poll() is None and time.monotonic() < deadline:
                    try:
                        packet, peer = server.recvfrom(4096)
                    except TimeoutError:
                        continue
                    name, qtype, question = read_question(packet)
                    queries.append(name)
                    if not answer:
                        continue
                    # Only the exact provider name gets a DNS answer. Its IPv4 answer is reserved.
                    data = b""
                    if name == HOST and qtype == 1:
                        data = b"\xc0\x0c" + struct.pack("!HHIH", 1, 1, 30, 4) + socket.inet_aton("198.18.0.1")
                    flags = 0x8180 if name == HOST else 0x8183
                    header = packet[:2] + struct.pack("!HHHHH", flags, 1, int(bool(data)), 0, 0)
                    server.sendto(header + question + data, peer)
                output, error = child.communicate(timeout=1)
                assert child.returncode == 0 and output == expected and not error, (output, error, queries)
                assert queries and set(queries) == {HOST}, queries
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait()
    return True


def main(worker):
    # Test a production worker only when no interface can carry external traffic.
    if sys.platform != "linux" or {name for _, name in socket.if_nameindex()} != {"lo"}:
        print("Public DNS worker test requires a loopback-only Linux namespace")
        return 77
    checks = (
        (socket.AF_INET, "127.0.0.2", DESTINATION_BLOCKED, True),
        (socket.AF_INET6, "::1", DESTINATION_BLOCKED, True),
        (socket.AF_INET, "127.0.0.2", TIMED_OUT, False),
    )
    for family, resolver, expected, answer in checks:
        if not sample(worker, family, resolver, expected, answer):
            print("Public DNS worker test could not bind loopback port 53")
            return 77
    print("IPv4 and IPv6 selection, absolute DNS names, reserved destination refusal and bounded DNS timeout")
    return 0


if __name__ == "__main__":
    sys.exit(main(Path(sys.argv[1]).resolve()))
