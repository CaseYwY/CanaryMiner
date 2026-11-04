"""Explicit synthetic public DER generation, no private-key files, network or trust installation.

Run with a separately supplied Python cryptography installation; not a production dependency or automatic download.
"""
from datetime import datetime, timedelta, timezone
from pathlib import Path
import argparse
import uuid
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID


def make(out):
    out.mkdir(parents=True, exist_ok=True)
    now = datetime.now(timezone.utc)
    for kind in ('valid', 'missing', 'stale', 'revoked', 'expired', 'future', 'wrongusage', 'badsig', 'wrongroot'):
        root_key = ec.generate_private_key(ec.SECP256R1())
        leaf_key = ec.generate_private_key(ec.SECP256R1())
        other_key = ec.generate_private_key(ec.SECP256R1())
        name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'fixture-' + uuid.uuid4().hex)])
        root = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(root_key.public_key())
                .serial_number(x509.random_serial_number()).not_valid_before(now - timedelta(days=7))
                .not_valid_after(now + timedelta(days=7)).add_extension(x509.BasicConstraints(ca=True, path_length=1), True)
                .add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False), True)
                .add_extension(x509.SubjectKeyIdentifier.from_public_key(root_key.public_key()), False)
                .sign(root_key, hashes.SHA256()))
        begin = now + timedelta(hours=1) if kind == 'future' else now - timedelta(days=3)
        end = now - timedelta(days=1) if kind == 'expired' else now + timedelta(days=3)
        leaf = (x509.CertificateBuilder().subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'fixture.test')]))
                .issuer_name(name).public_key(leaf_key.public_key()).serial_number(x509.random_serial_number())
                .not_valid_before(begin).not_valid_after(end).add_extension(x509.BasicConstraints(ca=False, path_length=None), True)
                .add_extension(x509.KeyUsage(True, False, False, False, False, False, False, False, False), True)
                .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.CLIENT_AUTH if kind == 'wrongusage' else ExtendedKeyUsageOID.SERVER_AUTH]), False)
                .add_extension(x509.SubjectAlternativeName([x509.DNSName('fixture.test')]), False)
                .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(root_key.public_key()), False)
                .sign(other_key if kind == 'badsig' else root_key, hashes.SHA256()))
        crl_builder = (x509.CertificateRevocationListBuilder().issuer_name(name).last_update(now - timedelta(days=2))
                       .next_update(now - timedelta(hours=1) if kind == 'stale' else now + timedelta(days=2))
                       .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(root_key.public_key()), False)
                       .add_extension(x509.CRLNumber(1), False))
        if kind == 'revoked':
            revoked = (x509.RevokedCertificateBuilder().serial_number(leaf.serial_number)
                       .revocation_date(now - timedelta(hours=1)).build())
            crl_builder = crl_builder.add_revoked_certificate(revoked)
        crl = crl_builder.sign(root_key, hashes.SHA256())
        if kind == 'wrongroot':
            root = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(other_key.public_key())
                    .serial_number(x509.random_serial_number()).not_valid_before(now - timedelta(days=7))
                    .not_valid_after(now + timedelta(days=7)).add_extension(x509.BasicConstraints(ca=True, path_length=1), True)
                    .add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False), True)
                    .sign(other_key, hashes.SHA256()))
        for suffix, value in (('root', root), ('leaf', leaf), ('crl', crl)):
            path = out / f'{kind}.{suffix}.der'
            with path.open('xb') as target:
                target.write(value.public_bytes(serialization.Encoding.DER))
    print('Generated nine unique-issuer public DER cases; no private keys written')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    make(parser.parse_args().output)
