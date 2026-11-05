"""Explicit owned connection-check lifecycle and optional loopback TLS. Never uses public endpoints or user identities."""
import argparse
import base64
import json
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
from test_forecast_lifecycle import process_identity, wait


def packet(host='ok.test', port=1, total=1000, pem=b'', private=None, enabled=True):
    parts = [b'CMTLSQ1\n', b'1\n', b'1\n' if enabled else b'0\n']
    def number(value):
        parts.append(str(value).encode('ascii') + b'\n')
    def blob(value):
        number(len(value))
        parts.extend((value, b'\n'))
    blob(host.encode('ascii'))
    number(port)
    number(total)
    blob(pem)
    number(1 if private else 0)
    if private:
        for collection in private:
            number(len(collection))
            for value in collection:
                blob(value)
    return b''.join(parts)


def run(probe, helper, request, cancel=None, env=None):
    command = [str(probe), str(helper)] + ([] if cancel is None else [str(cancel)])
    result = subprocess.run(command, input=request, capture_output=True, timeout=8, check=True, env=env)
    return json.loads(result.stdout)


def ready_pid(marker):
    observed = []
    def complete_marker():
        try:
            pid = int(marker.read_text(encoding='ascii'))
        except (OSError, ValueError):
            return False
        if pid <= 0:
            return False
        observed.append(pid)
        return True
    assert wait(complete_marker), 'owned helper did not publish a complete ready marker'
    return observed[-1]


def lifecycle(args):
    with tempfile.TemporaryDirectory(prefix='tls-check-lifecycle-', dir=args.scratch) as directory:
        root = Path(directory)
        helper = root / ('fixture_tls_check' + ('.exe' if os.name == 'nt' else ''))
        shutil.copy2(args.fixture, helper)
        env = dict(os.environ, CANARY_SYNTHETIC_SECRET='never-inherit-fixture-only', HTTPS_PROXY='never-inherit-fixture-only', SSLKEYLOGFILE='never-inherit-fixture-only')
        result = run(args.probe, helper, packet(), env=env)
        assert result['transport'] == 0 and result['started'] == 1 and result['status'] == 9 and result['stage'] == 4
        for host in ('malformed.test', 'trailing.test', 'overflow.test'):
            result = run(args.probe, helper, packet(host=host))
            assert result['started'] and result['transport'] == 3 and result['status'] == -1, (host, result)
        for phase in ('resolve', 'connect', 'trust', 'startup'):
            marker = root / 'fixture-tls-ready'
            marker.unlink(missing_ok=True)
            cancel_file = root / 'cancel-owned-check'
            cancel_file.unlink(missing_ok=True)
            parent = subprocess.Popen([str(args.probe), str(helper), '--cancel-file', str(cancel_file)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            try:
                parent.stdin.write(packet(host=f'stall-{phase}.test', total=2500))
                parent.stdin.close()
                alive, close = process_identity(ready_pid(marker))
                try:
                    assert alive()
                    # Trigger only after real phase entry and a live owned-child identity. A launch-
                    # relative timer could cancel before either observation on a slow scheduler.
                    cancel_file.touch()
                    parent.wait(timeout=5)
                    result = json.loads(parent.stdout.read())
                    assert result['started'] == 1 and result['transport'] == 4 and result['status'] == -1 and result['stage'] == -1 and result['tls'] == -1 and result['elapsedMs'] < 2000, (phase, result)
                    if phase == 'startup':
                        assert result['elapsedMs'] >= 1000, 'cancel did not wait for delayed phase entry'
                    assert not alive(), 'cancelled owned child remains alive'
                finally:
                    close()
            finally:
                if parent.poll() is None:
                    parent.kill()
                parent.wait(timeout=5)
                parent.stdout.close()
        for host in ('stall-resolve.test', 'stall-startup.test'):
            marker.unlink(missing_ok=True)
            result = run(args.probe, helper, packet(host=host, total=250))
            # The total deadline includes helper startup. It may expire before the owned child
            # reads its request/writes readiness; the cancellation cases above separately
            # require observed phase entry and reaping. Never turn a valid early timeout into
            # an assumed readiness guarantee or relax the actual deadline/result assertions.
            assert result['started'] == 1 and result['transport'] == 5 and result['status'] == -1 and result['stage'] == -1 and result['tls'] == -1 and result['elapsedMs'] < 1500, (host, marker.exists(), result)
        # A file monitor with no trigger must exit/join after both success and total timeout.
        # Neither path may hang waiting for a test-side action that will never arrive.
        for host, total, transport, status in (('ok.test', 1000, 0, 9), ('stall-startup.test', 250, 5, -1)):
            command = [str(args.probe), str(helper), '--cancel-file', str(root / 'never-triggered')]
            completed = subprocess.run(command, input=packet(host=host, total=total), capture_output=True, timeout=8, check=True)
            result = json.loads(completed.stdout)
            assert result['started'] == 1 and result['transport'] == transport and result['status'] == status and result['elapsedMs'] < 1500, (host, result)
        marker.unlink(missing_ok=True)
        parent = subprocess.Popen([str(args.probe), str(helper)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        try:
            parent.stdin.write(packet(host='stall-trust.test', total=60000))
            parent.stdin.close()
            alive, close = process_identity(ready_pid(marker))
            try:
                assert alive()
                parent.kill()
                parent.wait(timeout=5)
                assert wait(lambda: not alive()), 'owned child outlived parent'
            finally:
                close()
        finally:
            if parent.poll() is None:
                parent.kill()
            parent.wait(timeout=5)
            parent.stdout.close()
    for artifact in (args.parent_library, args.native_library):
        if artifact:
            data = artifact.read_bytes()
            for name in (b'fixture-tls-ready', b'stall-resolve.test', b'stall-connect.test', b'stall-trust.test', b'stall-startup.test', b'--cancel-file'):
                assert name not in data
    print('TLS check lifecycle: success/malformed/overflow, three labelled phases plus delayed startup, live-child-triggered cancel/reap, pre-ready total timeout, monitor completion, parent death and artifact separation passed')


def loopback(args):
    command = [sys.executable] if not args.wsl_distro else ['wsl', '-d', args.wsl_distro, '--exec', 'python3']
    # The returned JSON contains an ephemeral SYNTHETIC key. Never print subprocess output/material.
    generated = subprocess.run([*command, str(args.generator_script), 'valid', 'localhost'], capture_output=True, timeout=30, check=True)
    material = json.loads(generated.stdout)
    with tempfile.TemporaryDirectory(prefix='tls-check-loopback-', dir=args.scratch) as directory:
        root = Path(directory)
        cert, key = root / 'server.pem', root / 'server.key'
        cert.write_text(material['cert'], encoding='ascii')
        key.write_text(material['key'], encoding='ascii')
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        try:
            context.load_cert_chain(cert, key)
        finally:
            key.unlink(missing_ok=True)
        ca, crl = base64.b64decode(material['root']), base64.b64decode(material['crl'])
        del material, generated
        for mode in ('valid', 'tickets', 'untrusted', 'stall-timeout', 'stall-cancel'):
            # An inspection abort is not a promise that the peer consumed the final flight or
            # finished post-handshake ticket writes. Keep the stronger peer-completion assertion
            # in the no-ticket case, and separately exercise normal ticket issuance as local-only evidence.
            context.num_tickets = 2 if mode == 'tickets' else 0
            done = threading.Event()
            handshaken = threading.Event()
            received = bytearray()
            errors = []
            listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            listener.bind(('127.0.0.1', 0))
            listener.listen(1)
            listener.settimeout(5)
            def serve():
                try:
                    with listener:
                        channel, _ = listener.accept()
                        with channel:
                            channel.settimeout(5)
                            if mode.startswith('stall-'):
                                assert channel.recv(1), 'native check did not emit ClientHello'
                                done.wait(5)
                            else:
                                try:
                                    with context.wrap_socket(channel, server_side=True) as peer:
                                        handshaken.set()
                                        received.extend(peer.recv(4096))
                                except (ssl.SSLError, ConnectionError):
                                    pass  # negative trust or deliberate local abort without a graceful peer shutdown
                except Exception as error:
                    errors.append(type(error).__name__)
            thread = threading.Thread(target=serve, daemon=True)
            thread.start()
            private = ([ca], [], [crl]) if os.name == 'nt' and mode != 'untrusted' else None
            pem = ssl.DER_cert_to_PEM_cert(ca).encode('ascii') if os.name != 'nt' and mode != 'untrusted' else b''
            try:
                result = run(args.probe, args.native, packet('localhost', listener.getsockname()[1], 350 if mode == 'stall-timeout' else 2000, pem, private),
                             cancel=750 if mode == 'stall-cancel' else None)
            finally:
                done.set()
                thread.join(timeout=6)
                listener.close()
            assert not thread.is_alive() and not errors and not received, (mode, errors, len(received), result)
            if mode in ('valid', 'tickets'):
                assert result['transport'] == 0 and result['status'] == 9 and result['stage'] == 4, (mode, result)
                if mode == 'valid':
                    assert handshaken.is_set(), 'no-ticket peer did not finish the actual handshake'
            elif mode == 'untrusted':
                assert result['transport'] == 0 and result['status'] == 8 and result['stage'] == 3 and result['tls'] == 4, (mode, result)
            elif mode == 'stall-cancel':
                assert result['transport'] == 4 and result['status'] == -1 and result['elapsedMs'] < 1500, (mode, result)
            else:
                assert result['transport'] == 5 and result['status'] == -1 and result['elapsedMs'] < 1500, (mode, result)
    print('TLS check loopback: no-ticket peer completion, ticket local-verification scope, normal untrusted refusal, stalled handshake timeout/cancel; no application bytes or public endpoints')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('test assertions require non-optimized Python')
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--fixture', type=Path)
    parser.add_argument('--native', type=Path)
    parser.add_argument('--parent-library', type=Path)
    parser.add_argument('--native-library', type=Path)
    parser.add_argument('--scratch', type=Path)
    parser.add_argument('--wsl-distro')
    parser.add_argument('--generator-script')
    args = parser.parse_args()
    if args.scratch:
        args.scratch.mkdir(parents=True, exist_ok=True)
    if args.fixture:
        lifecycle(args)
    if args.native:
        loopback(args)
