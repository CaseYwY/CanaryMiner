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
import time
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


def alternate_loopback(args, context, ca, crl):
    # Match the helper's native lookup order, and keep all traffic on owned loopback sockets.
    resolved = socket.getaddrinfo('localhost', '1', socket.AF_UNSPEC, socket.SOCK_STREAM,
                                  socket.IPPROTO_TCP, socket.AI_NUMERICSERV)
    selected = list(dict.fromkeys((family, address[0]) for family, _, _, _, address in resolved))
    assert set(selected) == {(socket.AF_INET, '127.0.0.1'), (socket.AF_INET6, '::1')}, selected
    context.num_tickets = 0

    for mode, total in (('recover', 5000), ('untrusted', 5000), ('protocol', 5000), ('cancel', 5000),
                        ('total-timeout', 3000), ('only-first-slow', 3600)):
        first_family, first_address = selected[0]
        other_family, other_address = selected[1]
        stop = threading.Event()
        hellos = [threading.Event(), threading.Event()]
        completed = [threading.Event(), threading.Event()]
        first_closed = threading.Event()
        errors = []

        def serve(listener, index, behavior):
            try:
                while not stop.is_set():
                    try:
                        channel, _ = listener.accept()
                    except socket.timeout:
                        continue
                    break
                else:
                    return
                with channel:
                    channel.settimeout(7)
                    if behavior == 'silent':
                        assert channel.recv(1) == b'\x16', 'expected a TLS handshake record'
                        hellos[index].set()
                        try:
                            while channel.recv(4096):
                                pass
                        except ConnectionResetError:
                            pass  # owned helper cancellation can reset the socket on Windows
                        if index == 0:
                            first_closed.set()
                        return
                    if behavior == 'protocol':
                        assert channel.recv(4096).startswith(b'\x16'), 'expected ClientHello'
                        channel.sendall(b'\x15\x03\x03\x00\x02\x02\x28')  # fatal handshake_failure
                        stop.wait(6)
                        return
                    if behavior == 'slow':
                        time.sleep(2.1)
                    if index == 1:
                        assert first_closed.wait(0.75), 'stalled first connection was not retired'
                    try:
                        with context.wrap_socket(channel, server_side=True) as peer:
                            completed[index].set()
                            assert not peer.recv(1), 'preflight sent an application byte'
                    except (ssl.SSLError, ConnectionError):
                        if behavior != 'untrusted' and not completed[index].is_set():
                            raise
            except Exception as error:
                errors.append((index, type(error).__name__, str(error)))

        with socket.socket(first_family, socket.SOCK_STREAM) as first, \
             socket.socket(other_family, socket.SOCK_STREAM) as alternate:
            for listener in (first, alternate):
                if listener.family == socket.AF_INET6:
                    listener.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
                listener.settimeout(0.1)
            first.bind((first_address, 0))
            port = first.getsockname()[1]
            alternate.bind((other_address, port))
            first.listen(1)
            threads = []
            behavior = {'only-first-slow': 'slow', 'untrusted': 'untrusted', 'protocol': 'protocol'}.get(mode, 'silent')
            threads.append(threading.Thread(target=serve, args=(first, 0, behavior), daemon=True))
            if mode != 'only-first-slow':
                alternate.listen(1)
                later = 'trusted' if mode == 'recover' else 'silent'
                threads.append(threading.Thread(target=serve, args=(alternate, 1, later), daemon=True))
            for thread in threads:
                thread.start()
            try:
                private = ([ca], [], [crl]) if os.name == 'nt' and mode != 'untrusted' else None
                pem = ssl.DER_cert_to_PEM_cert(ca).encode('ascii') if os.name != 'nt' and mode != 'untrusted' else b''
                reply = run(args.probe, args.native, packet('localhost', port, total, pem, private),
                            cancel=800 if mode == 'cancel' else None)
            finally:
                stop.set()
                for thread in threads:
                    thread.join(timeout=8)
            assert not any(thread.is_alive() for thread in threads) and not errors, (mode, errors)
            if mode in ('recover', 'only-first-slow'):
                assert (reply['transport'], reply['status'], reply['stage']) == (0, 9, 4), (mode, reply)
                assert completed[1 if mode == 'recover' else 0].is_set(), (mode, reply)
                assert hellos[0].is_set() == (mode == 'recover'), (mode, reply)
            elif mode == 'untrusted':
                assert (reply['transport'], reply['status'], reply['stage'], reply['tls']) == (0, 8, 3, 4), reply
                assert not hellos[1].is_set(), 'trust failure admitted another ClientHello'
            elif mode == 'protocol':
                # Schannel reports this fatal alert as verification failure.
                expected_error = 4 if os.name == 'nt' else 5
                assert (reply['transport'], reply['status'], reply['stage'], reply['tls']) == (0, 8, 3, expected_error), reply
                assert not hellos[1].is_set(), 'protocol failure admitted another ClientHello'
            elif mode == 'cancel':
                assert reply['transport'] == 4 and reply['status'] == -1 and reply['elapsedMs'] < 1800, reply
                assert hellos[0].is_set() and not hellos[1].is_set(), reply
            else:
                assert reply['elapsedMs'] < total + 600, reply
                assert (reply['transport'] == 5 and reply['status'] == -1) or (
                    reply['transport'] == 0 and reply['status'] == 4 and reply['stage'] == 3), reply
                assert hellos[0].is_set() and hellos[1].is_set(), reply

    print('TLS address recovery: alternate opens after a stall, trust, protocol and cancel stop, one slow peer stays, original deadline holds')


def generate_material(args, mode):
    command = [sys.executable] if not args.wsl_distro else ['wsl', '-d', args.wsl_distro, '--exec', 'python3']
    # The returned JSON contains an ephemeral SYNTHETIC key. Never print subprocess output/material.
    generated = subprocess.run([*command, str(args.generator_script), mode, 'localhost'],
                               capture_output=True, timeout=30, check=True)
    return json.loads(generated.stdout)


def schannel_chain_loopback(args):
    other_anchor = base64.b64decode(generate_material(args, 'valid')['root'])
    for mode in ('received', 'omitted', 'explicit', 'duplicate', 'untrusted-root', 'missing-crl', 'revoked', 'chain-cap'):
        material = generate_material(args, 'chain-revoked' if mode == 'revoked' else 'chain')
        ca = base64.b64decode(material['root'])
        intermediate = ssl.PEM_cert_to_DER_cert(material['intermediate'])
        crls = [base64.b64decode(material['crl'])]
        if mode != 'missing-crl':
            crls.append(base64.b64decode(material['intermediate_crl']))
        explicit = [intermediate] if mode in ('explicit', 'duplicate') else []
        chain = material['cert']
        if mode not in ('omitted', 'explicit'):
            chain += material['intermediate']
        if mode in ('duplicate', 'untrusted-root'):
            chain += ssl.DER_cert_to_PEM_cert(ca)
        if mode == 'chain-cap':
            for _ in range(16):
                extra = base64.b64decode(generate_material(args, 'valid')['root'])
                chain += ssl.DER_cert_to_PEM_cert(extra)
        with tempfile.TemporaryDirectory(prefix='schannel-chain-', dir=args.scratch) as directory:
            cert, key = Path(directory) / 'server.pem', Path(directory) / 'server.key'
            cert.write_text(chain, encoding='ascii')
            key.write_text(material['key'], encoding='ascii')
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.minimum_version = ssl.TLSVersion.TLSv1_2
            context.num_tickets = 0
            try:
                context.load_cert_chain(cert, key)
            finally:
                key.unlink(missing_ok=True)
        del material

        finished = threading.Event()
        errors = []
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            listener.listen(1)
            listener.settimeout(5)

            def serve():
                try:
                    channel, _ = listener.accept()
                    with channel:
                        channel.settimeout(5)
                        with context.wrap_socket(channel, server_side=True) as peer:
                            finished.set()
                            assert not peer.recv(1), 'preflight sent an application byte'
                except (ssl.SSLError, ConnectionResetError):
                    pass  # Negative trust or the preflight worker's local abort.
                except Exception as error:
                    errors.append(type(error).__name__)

            thread = threading.Thread(target=serve, daemon=True)
            thread.start()
            anchor = other_anchor if mode == 'untrusted-root' else ca
            try:
                reply = run(args.probe, args.native, packet('localhost', listener.getsockname()[1], 3500,
                            private=([anchor], explicit, crls)))
            finally:
                thread.join(timeout=6)
            assert not errors and not thread.is_alive(), (mode, errors)
        expected = (0, 9, 4, 0) if mode in ('received', 'explicit', 'duplicate') else (0, 8, 3, 4)
        assert (reply['transport'], reply['status'], reply['stage'], reply['tls']) == expected, (mode, reply)
        if mode in ('received', 'explicit', 'duplicate'):
            assert finished.is_set(), mode
    print('Schannel peer chain: received and explicit intermediates, duplicates, missing chain and CRL, untrusted root, revoked intermediate, support limit')


def loopback(args):
    material = generate_material(args, 'valid')
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
        del material
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
        alternate_loopback(args, context, ca, crl)
    if os.name == 'nt':
        schannel_chain_loopback(args)
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
