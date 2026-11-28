"""Exercise the retained Linux TLS worker with a peer that supplies signed OCSP staples."""
import argparse
from datetime import datetime, timedelta, timezone
import json
import os
from pathlib import Path
import re
import selectors
import subprocess
import tempfile
import time

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509 import ocsp
from cryptography.x509.oid import ExtendedKeyUsageOID, ExtensionOID, NameOID

from test_pool_tls_check_process import packet


def certificate(now, name, issuer=None, *, features=(), responder=False, intermediate=False, invalid_feature=False):
    key = ec.generate_private_key(ec.SECP256R1())
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, name)])
    issuer_name = issuer[0].subject if issuer else subject
    cert = (x509.CertificateBuilder().subject_name(subject).issuer_name(issuer_name)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now - timedelta(days=14)).not_valid_after(now + timedelta(days=3))
            .add_extension(x509.BasicConstraints(ca=issuer is None or intermediate, path_length=None), critical=True))
    if issuer and not intermediate:
        purpose = ExtendedKeyUsageOID.OCSP_SIGNING if responder else ExtendedKeyUsageOID.SERVER_AUTH
        cert = cert.add_extension(x509.ExtendedKeyUsage([purpose]), critical=False)
        if not responder:
            cert = cert.add_extension(x509.SubjectAlternativeName([x509.DNSName("localhost")]), critical=False)
    else:
        cert = cert.add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False),
                                  critical=True)
    if features:
        cert = cert.add_extension(x509.TLSFeature(features), critical=False)
    if invalid_feature:
        cert = cert.add_extension(x509.UnrecognizedExtension(ExtensionOID.TLS_FEATURE,
                                  b"\x30\x03\x02\x01\x05\x00"), critical=False)
    return cert.sign(issuer[1] if issuer else key, hashes.SHA256()), key


def staple(cert, issuer, this_update, next_update, *, state=ocsp.OCSPCertStatus.GOOD,
           signer=None, digest=None, extension=None):
    delegate = signer
    signer = signer or issuer
    revoked_at = this_update if state is ocsp.OCSPCertStatus.REVOKED else None
    reason = x509.ReasonFlags.key_compromise if revoked_at else None
    response = (ocsp.OCSPResponseBuilder()
                .add_response(cert, issuer[0], digest or hashes.SHA1(), state, this_update, next_update,
                              revoked_at, reason)
                .responder_id(ocsp.OCSPResponderEncoding.NAME, signer[0]))
    if delegate:
        response = response.certificates([delegate[0]])
    if extension is not None:
        response = response.add_extension(extension, critical=True)
    return response.sign(signer[1], hashes.SHA256()).public_bytes(serialization.Encoding.DER)


def der_object(tag, payload):
    length = len(payload)
    if length < 128:
        return bytes([tag, length]) + payload
    length_bytes = length.to_bytes((length.bit_length() + 7) // 8, "big")
    return bytes([tag, 0x80 | len(length_bytes)]) + length_bytes + payload


def staple_with_trailing_basic_data(stapled_response):
    # The fixed BasicOCSPResponse OID directly precedes the response OCTET STRING.
    basic_oid = bytes.fromhex("06092b0601050507300101")
    _, encoded_payload = stapled_response.split(basic_oid, 1)
    assert encoded_payload[0] == 4
    length_bytes = (encoded_payload[1] & 0x7F) if encoded_payload[1] >= 128 else 0
    basic = encoded_payload[2 + length_bytes:]
    wrapped = der_object(0x30, basic_oid + der_object(4, basic + b"\x00"))
    return der_object(0x30, b"\x0a\x01\x00" + der_object(0xA0, wrapped))


def listening_port(server):
    buffer = bytearray()
    deadline = time.monotonic() + 6
    with selectors.DefaultSelector() as selector:
        selector.register(server.stdout, selectors.EVENT_READ)
        while len(buffer) < 8192 and (remaining := deadline - time.monotonic()) > 0 and selector.select(remaining):
            chunk = os.read(server.stdout.fileno(), 4096)
            if not chunk:
                break
            buffer.extend(chunk)
            match = re.search(rb"^ACCEPT 127\.0\.0\.1:([0-9]+)\r?\n", buffer, re.M)
            if match:
                return int(match[1])
    raise AssertionError("owned localhost server did not announce its port")


def probe_result(args, port, authority):
    result = subprocess.run([args.probe, args.worker, "inspect"],
                            input=packet("localhost", port, 5000, authority.public_bytes(serialization.Encoding.PEM)),
                            capture_output=True, timeout=8, check=True)
    return json.loads(result.stdout)


def inspect(args, root, leaf, authority, response, protocol, chain=None):
    cert_file, key_file, status_file = (root / name for name in ("leaf.pem", "server.key", "response.der"))
    cert_file.write_bytes(leaf[0].public_bytes(serialization.Encoding.PEM))
    key_file.write_bytes(leaf[1].private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                              serialization.NoEncryption()))
    if response is not None:
        status_file.write_bytes(response)
    command = [args.openssl, "s_server", "-accept", "127.0.0.1:0", "-naccept", "1",
               "-cert", cert_file, "-key", key_file, protocol]
    if response is not None:
        command.extend(["-status_file", status_file])
    if chain is not None:
        chain_file = root / "chain.pem"
        chain_file.write_bytes(chain.public_bytes(serialization.Encoding.PEM))
        command.extend(["-cert_chain", chain_file])
    with subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL) as server:
        try:
            return probe_result(args, listening_port(server), authority)
        finally:
            if server.poll() is None:
                server.terminate()
                try:
                    server.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait(timeout=2)


def main(args):
    now = datetime.now(timezone.utc).replace(microsecond=0)
    past, future = now - timedelta(hours=1), now + timedelta(hours=8)
    issuer = certificate(now, "canary-ocsp-fixture")
    alternate = certificate(now, "canary-ocsp-fixture")
    plain = certificate(now, "optional-staple", issuer)
    status_request = x509.TLSFeatureType.status_request
    status_request_v2 = x509.TLSFeatureType.status_request_v2
    must = certificate(now, "must-staple", issuer, features=[status_request])
    unsupported = certificate(now, "unsupported-status-request", issuer, features=[status_request_v2])
    mixed = certificate(now, "mixed-status-request", issuer, features=[status_request, status_request_v2])
    malformed = certificate(now, "malformed-tls-feature", issuer, invalid_feature=True)
    signer = certificate(now, "delegated-ocsp", issuer, responder=True)
    issuing_ca = certificate(now, "issuing-ocsp-fixture", issuer, intermediate=True)
    issued_leaf = certificate(now, "intermediate-issued-must-staple", issuing_ca, features=[status_request])
    issued_status = staple(issued_leaf[0], issuing_ca, past, future)
    requiring_root = certificate(now, "status-requiring-root", features=[status_request])
    requiring_ca = certificate(now, "requiring-issuer", issuer, intermediate=True, features=[status_request])
    missing_ca = certificate(now, "issuer-without-root-feature", requiring_root, intermediate=True)
    inherited_ca = certificate(now, "issuer-with-root-feature", requiring_root, intermediate=True,
                               features=[status_request])
    unsupported_ca = certificate(now, "issuer-unsupported-feature", issuer, intermediate=True,
                                 features=[status_request_v2])
    malformed_ca = certificate(now, "issuer-malformed-feature", issuer, intermediate=True, invalid_feature=True)
    chains = [
        ("issuer feature omitted", issuer, requiring_ca, (), False, False),
        ("issuer feature omitted despite valid staple", issuer, requiring_ca, (), True, False),
        ("issuer feature inherited with valid staple", issuer, requiring_ca, [status_request], True, True),
        ("issuer feature inherited without staple", issuer, requiring_ca, [status_request], False, False),
        ("trust anchor feature omitted by issuer", requiring_root, missing_ca, [status_request], True, False),
        ("trust anchor feature inherited by whole chain", requiring_root, inherited_ca, [status_request], True, True),
        ("unsupported issuer feature", issuer, unsupported_ca, [status_request], True, False),
        ("malformed issuer feature", issuer, malformed_ca, [status_request], True, False),
    ]
    good = staple(must[0], issuer, past, future)
    cases = [
        ("optional absent", plain, None, True),
        ("required good", must, good, True),
        ("required absent", must, None, False),
        ("unsupported required feature", unsupported, None, False),
        ("unsupported feature despite valid staple", mixed, staple(mixed[0], issuer, past, future), False),
        ("malformed feature", malformed, staple(malformed[0], issuer, past, future), False),
        ("trailing basic response data", must, staple_with_trailing_basic_data(good), False),
        ("unsupported critical response extension", must, staple(must[0], issuer, past, future,
            extension=x509.UnrecognizedExtension(x509.ObjectIdentifier("1.3.6.1.4.1.32473.1"), b"\x05\x00")), False),
        ("try later", must, ocsp.OCSPResponseBuilder.build_unsuccessful(
            ocsp.OCSPResponseStatus.TRY_LATER).public_bytes(serialization.Encoding.DER), False),
        ("revoked", plain, staple(plain[0], issuer, past, future, state=ocsp.OCSPCertStatus.REVOKED), False),
        ("unknown", must, staple(must[0], issuer, past, future, state=ocsp.OCSPCertStatus.UNKNOWN), False),
        ("wrong leaf", must, staple(plain[0], issuer, past, future), False),
        ("wrong issuer", must, staple(must[0], alternate, past, future), False),
        ("wrong signer", must, staple(must[0], issuer, past, future, signer=alternate), False),
        ("signer without OCSP purpose", must, staple(must[0], issuer, past, future, signer=plain), False),
        ("delegated good", must, staple(must[0], issuer, past, future, signer=signer, digest=hashes.SHA256()), True),
        ("expired", must, staple(must[0], issuer, past, now - timedelta(minutes=15)), False),
        ("future", must, staple(must[0], issuer, now + timedelta(minutes=15), future), False),
        ("fresh with next", must, staple(must[0], issuer, now - timedelta(days=4), future), True),
        ("stale with next", must, staple(must[0], issuer, now - timedelta(days=11), future), False),
        ("fresh without next", must, staple(must[0], issuer, past, None), True),
        ("stale without next", must, staple(must[0], issuer, now - timedelta(days=2), None), False),
    ]
    with tempfile.TemporaryDirectory(prefix="pool-ocsp-", dir=args.scratch) as directory:
        for protocol in ("-tls1_2", "-tls1_3"):
            for label, leaf, response, expected in cases:
                result = inspect(args, Path(directory), leaf, issuer[0], response, protocol)
                assert result["ok"] == expected, (protocol, label, result)
                if not expected:
                    assert result["tls"] == 4 and result["stage"] == 3, (protocol, label, result)
            result = inspect(args, Path(directory), issued_leaf, issuer[0], issued_status, protocol, chain=issuing_ca[0])
            assert result["ok"] == 1 and result["tls"] == 0, (protocol, "verified intermediate issuer", result)
            for label, root, ca, features, supply_staple, expected in chains:
                child = certificate(now, label, ca, features=features)
                response = staple(child[0], ca, past, future) if supply_staple else None
                result = inspect(args, Path(directory), child, root[0], response, protocol, chain=ca[0])
                assert result["ok"] == expected, (protocol, label, result)
                if not expected:
                    assert result["tls"] == 4 and result["stage"] == 3, (protocol, label, result)
    print(f"Linux native OCSP stapling: {(len(cases) + len(chains) + 1) * 2} checks")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    for arg in ("openssl", "probe", "worker", "scratch"):
        parser.add_argument(f"--{arg}", type=lambda value: Path(value).resolve(), required=True)
    main(parser.parse_args())
