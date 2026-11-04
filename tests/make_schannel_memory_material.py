"""Explicit test-owned ephemeral material on stdout for a capturing driver, NEVER a user identity/export helper.

Unlike the public-DER inspector generator, this emits a synthetic leaf key to the private test process only.
Do not log its JSON or write it in the repository.
"""
import base64
import json
import sys
import uuid
from datetime import datetime, timedelta, timezone
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID

def material(mode):
    now = datetime.now(timezone.utc)
    root_key = ec.generate_private_key(ec.SECP256R1())
    key = ec.generate_private_key(ec.SECP256R1())
    issuer = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'fixture-' + uuid.uuid4().hex)])
    root = (x509.CertificateBuilder().subject_name(issuer).issuer_name(issuer).public_key(root_key.public_key())
            .serial_number(x509.random_serial_number()).not_valid_before(now - timedelta(days=7)).not_valid_after(now + timedelta(days=7))
            .add_extension(x509.BasicConstraints(ca=True, path_length=1), True)
            .add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False), True)
            .add_extension(x509.SubjectKeyIdentifier.from_public_key(root_key.public_key()), False).sign(root_key, hashes.SHA256()))
    leaf_builder = (x509.CertificateBuilder().subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'fixture.test')]))
            .issuer_name(issuer).public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now + timedelta(hours=1) if mode == 'future' else now - timedelta(days=3))
            .not_valid_after(now - timedelta(days=1) if mode == 'expired' else now + timedelta(days=3))
            .add_extension(x509.BasicConstraints(ca=False, path_length=None), True)
            .add_extension(x509.KeyUsage(True, False, False, False, False, False, False, False, False), True)
            .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.CLIENT_AUTH if mode == 'wrongusage' else ExtendedKeyUsageOID.SERVER_AUTH]), False)
            .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(root_key.public_key()), False))
    if mode != 'nosan':
        leaf_builder = leaf_builder.add_extension(x509.SubjectAlternativeName([x509.DNSName('wrong-fixture.test' if mode == 'wrongsan' else 'fixture.test')]), False)
    leaf = leaf_builder.sign(root_key, hashes.SHA256())
    builder = (x509.CertificateRevocationListBuilder().issuer_name(issuer).last_update(now - timedelta(days=2))
               .next_update(now - timedelta(hours=1) if mode == 'stale' else now + timedelta(days=2))
               .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(root_key.public_key()), False).add_extension(x509.CRLNumber(1), False))
    if mode == 'revoked':
        builder = builder.add_revoked_certificate(x509.RevokedCertificateBuilder().serial_number(leaf.serial_number).revocation_date(now - timedelta(hours=1)).build())
    crl = builder.sign(root_key, hashes.SHA256())
    if mode == 'wrongroot':
        other = ec.generate_private_key(ec.SECP256R1())
        root = (x509.CertificateBuilder().subject_name(issuer).issuer_name(issuer).public_key(other.public_key())
                .serial_number(x509.random_serial_number()).not_valid_before(now - timedelta(days=7)).not_valid_after(now + timedelta(days=7))
                .add_extension(x509.BasicConstraints(ca=True, path_length=1), True)
                .add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False), True)
                .sign(other, hashes.SHA256()))
    return {'root': base64.b64encode(root.public_bytes(serialization.Encoding.DER)).decode('ascii'),
            'crl': base64.b64encode(crl.public_bytes(serialization.Encoding.DER)).decode('ascii'),
            'cert': leaf.public_bytes(serialization.Encoding.PEM).decode('ascii'),
            'key': key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()).decode('ascii')}


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('explicit synthetic case required; output is for a capturing test driver only')
    print(json.dumps(material(sys.argv[1])))
