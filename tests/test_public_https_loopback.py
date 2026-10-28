"""Explicit offline TLS integration. No trust-store install, public API, GPU, or stored certificates.

Run with the separately compiled public_https_probe and an OpenSSL executable. Keys/CA are ephemeral.
Not part of default CPU-only CTest: opt in with CANARY_HTTPS_LOOPBACK_TESTS.
"""
import argparse
import contextlib
from datetime import datetime, timedelta, timezone
import os
from pathlib import Path
import socketserver
import shutil
import ssl
import sys
import subprocess
import tempfile
import threading
import time


BODY = b'{"fixture":true}'
STATUS = {name: index for index, name in enumerate(("ok", "unavailable", "invalid-request", "busy", "cancelled", "timeout",
          "destination-blocked", "tls-verification", "tls-protocol", "network", "http-status", "invalid-headers", "body-too-large", "invalid-json"))}


def certificates(root, openssl):
    def run(*args):
        result = subprocess.run([openssl, *args], cwd=root, capture_output=True, timeout=20)
        if result.returncode:
            raise RuntimeError(f"Synthetic certificate generation failed: {result.stderr.decode(errors='replace')}")
    for name in ("root", "untrusted"):
        run("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2", "-subj", f"/CN=Canary Synthetic {name}",
            "-keyout", f"{name}.key", "-out", f"{name}.pem", "-addext", "basicConstraints=critical,CA:TRUE",
            "-addext", "keyUsage=critical,keyCertSign,cRLSign,digitalSignature,keyEncipherment", "-addext", "subjectAltName=DNS:fixture.test.invalid")
    run("req", "-new", "-newkey", "rsa:2048", "-nodes", "-subj", "/CN=fixture.test.invalid", "-keyout", "leaf.key", "-out", "leaf.csr")
    (root / "extensions.cnf").write_text("basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\nsubjectAltName=DNS:fixture.test.invalid\n", encoding="ascii")
    run("x509", "-req", "-in", "leaf.csr", "-CA", "root.pem", "-CAkey", "root.key", "-set_serial", "42",
        "-days", "2", "-out", "leaf.pem", "-extfile", "extensions.cnf")
    (root / "index.txt").touch()
    (root / "serial").write_text("43\n", encoding="ascii")
    (root / "ca.cnf").write_text("[ca]\ndefault_ca=fixture\n[fixture]\ndatabase=index.txt\nserial=serial\nnew_certs_dir=.\ncertificate=root.pem\nprivate_key=root.key\ndefault_md=sha256\npolicy=names\n[names]\ncommonName=supplied\n", encoding="ascii")
    now = datetime.now(timezone.utc)
    run("ca", "-batch", "-config", "ca.cnf", "-in", "leaf.csr", "-out", "expired.pem", "-notext", "-extfile", "extensions.cnf",
        "-startdate", (now - timedelta(days=2)).strftime("%Y%m%d%H%M%SZ"), "-enddate", (now - timedelta(days=1)).strftime("%Y%m%d%H%M%SZ"))


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, root, cert="root", raw=False):
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.minimum_version = ssl.TLSVersion.TLSv1_2
        self.context.load_cert_chain(root / f"{cert}.pem", root / ("root.key" if cert == "root" else "leaf.key"))
        self.raw = raw
        self.requests = []
        self.sni = []
        self.context.set_servername_callback(lambda connection, name, context: self.sni.append(name))
        super().__init__(("127.0.0.1", 0), Handler)


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        connection = self.request
        try:
            if self.server.raw:
                connection.recv(1024)
                connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n")
                return
            connection.settimeout(3)
            connection = self.server.context.wrap_socket(connection, server_side=True)
            request = b""
            while b"\r\n\r\n" not in request and len(request) < 8192:
                data = connection.recv(2048)
                if not data:
                    return
                request += data
            self.server.requests.append(request)
            path = request.split(b" ")[1]
            code, body, extras = b"200 OK", BODY, b""
            if path == b"/slow-header":
                time.sleep(2)
            if path == b"/redirect":
                code, extras = b"302 Found", b"Location: https://fixture.test.invalid/no-follow\r\n"
            if path == b"/http-error":
                code = b"503 Service Unavailable"
            if path == b"/rate-limit":
                code, extras = b"429 Too Many Requests", b"Retry-After: 999999\r\n"
            if path == b"/bad-json":
                body = b'{"duplicate":1,"duplicate":2}'
            if path == b"/invalid-utf8":
                body = b'{"x":"\xed\xa0\x80"}'
            if path == b"/deep-json":
                body = b"[" * 17 + b"0" + b"]" * 17
            if path == b"/large-body":
                body = b" " * 9000
            if path == b"/compressed":
                extras = b"Content-Encoding: gzip\r\n"
            if path == b"/large-header":
                extras = b"X-Fill: " + b"x" * 2100 + b"\r\n"
            media = b"text/html" if path == b"/html" else b"application/json"
            if path == b"/conflict-header":
                extras = b"Content-Type: text/html\r\n"
            header = b"HTTP/1.1 " + code + b"\r\nContent-Type: " + media + b"\r\nConnection: close\r\n" + extras
            if path in (b"/chunked", b"/chunked-large"):
                connection.sendall(header + b"Transfer-Encoding: chunked\r\n\r\n")
                chunks = [BODY] if path == b"/chunked" else [b"x" * 4096] * 3
                for chunk in chunks:
                    connection.sendall(f"{len(chunk):x}\r\n".encode() + chunk + b"\r\n")
                connection.sendall(b"0\r\n\r\n")
            else:
                length = len(body) + (10 if path == b"/truncated" else 0)
                connection.sendall(header + f"Content-Length: {length}\r\n\r\n".encode())
                if path == b"/slow-body":
                    connection.sendall(body[:1])
                    time.sleep(2)
                    connection.sendall(body[1:])
                else:
                    connection.sendall(body)
        except (OSError, ssl.SSLError):
            pass  # peer intentionally aborts on policy errors/cancellation
        finally:
            connection.close()


@contextlib.contextmanager
def serve(root, **kwargs):
    server = Server(root, **kwargs)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield server
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe", type=Path)
    parser.add_argument("--openssl", default="openssl")
    parser.add_argument("--fixture-worker", type=Path, help="Also test TLS requests through the real process boundary")
    args = parser.parse_args()
    args.probe = args.probe.resolve(strict=True)
    checks = 0
    with tempfile.TemporaryDirectory(prefix="canary-https-", dir=os.environ.get("CANARY_TEST_TMP")) as temp:
        root = Path(temp)
        certificates(root, args.openssl)
        helper = None
        if args.fixture_worker:
            helper = root / ("synthetic helper" + args.fixture_worker.suffix)
            shutil.copy2(args.fixture_worker.resolve(strict=True), helper)
        def case(server, path, expected, mode="normal", ca="root", retry=0):
            nonlocal checks
            before = len(server.requests)
            # Ambient proxies must not receive even the synthetic request. No inherited credentials are supplied.
            env = dict(os.environ, HTTPS_PROXY="http://127.0.0.1:1", ALL_PROXY="http://127.0.0.1:1", NO_PROXY="")
            command = [str(args.probe), str(server.server_address[1]), path, str(root / f"{ca}.pem"), mode, str(STATUS[expected]), str(retry)]
            if helper:
                command.append(str(helper))
            result = subprocess.run(command,
                                    capture_output=True, text=True, timeout=5, env=env)
            assert result.returncode == 0, f"{path}/{mode}: {result.stdout} {result.stderr} exit={result.returncode}"
            print(f"PASS {path}/{mode}: {result.stdout.strip()}")
            checks += 1
            assert len(server.requests) - before <= 1, "redirect/retry unexpectedly performed"
            if expected in ("tls-verification", "tls-protocol", "destination-blocked"):
                assert len(server.requests) == before, "HTTP bytes sent before verified handshake"
            if expected == "ok":
                assert server.sni[-1] == "fixture.test.invalid", "SNI identity missing or changed"
            for request in server.requests[before:]:
                lower = request.lower()
                assert b"authorization:" not in lower and b"cookie:" not in lower
                assert b"accept-encoding: identity" in lower
        with serve(root) as server:
            # Status enum values are stable within this build; probe compares exact status and empty failure output.
            for path, status in (("/ok", "ok"), ("/chunked", "ok"), ("/redirect", "http-status"), ("/http-error", "http-status"),
                                 ("/html", "invalid-headers"), ("/compressed", "invalid-headers"), ("/conflict-header", "invalid-headers"), ("/large-header", "invalid-headers"),
                                 ("/large-body", "body-too-large"), ("/chunked-large", "body-too-large"), ("/bad-json", "invalid-json"), ("/invalid-utf8", "invalid-json"),
                                 ("/deep-json", "invalid-json"), ("/truncated", "network"), ("/slow-header", "timeout"), ("/slow-body", "timeout")):
                case(server, path, status)
            case(server, "/rate-limit", "http-status", retry=3600)
            case(server, "/slow-header", "cancelled", mode="cancel")
            case(server, "/slow-body", "cancelled", mode="cancel")
            case(server, "/ok", "destination-blocked", mode="blocked")
            case(server, "/ok", "tls-verification", mode="wrong-name")
            case(server, "/ok", "tls-verification", ca="untrusted")
        with serve(root, cert="expired") as server:
            case(server, "/ok", "tls-verification")
            assert not server.requests
        with serve(root, cert="leaf") as server:
            # No CRL/OCSP is advertised in this synthetic subordinate certificate. Keep native policy visible:
            # strict Schannel refuses unknown revocation; OpenSSL's default verification does not fetch revocation.
            case(server, "/ok", "tls-verification" if sys.platform == "win32" else "ok")
        with serve(root, raw=True) as server:
            case(server, "/ok", "tls-protocol")
            assert not server.requests
    print(f"public HTTPS loopback: {checks} cases passed; no public network or trust-store writes")


if __name__ == "__main__":
    main()
