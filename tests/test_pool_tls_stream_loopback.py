"""Explicit retained native TLS helper against localhost-only synthetic peers. No real wallet/proof/GPU/public endpoint."""
import argparse
import base64
import json
import os
from pathlib import Path
import ssl
import socket
import subprocess
import sys
import tempfile
import threading
from test_pool_tls_check_process import packet


def notification(job='native-rpc-job', tag=1):
    header = bytes((tag + i) % 256 for i in range(76)).hex()
    return json.dumps({'id': None, 'method': 'mining.notify', 'params': {'job_id': job, 'header': header,
                       'target': '0' * 60 + 'ffff', 'height': 139053, 'cert_version': 3}}, separators=(',', ':')).encode() + b'\n'


def main(args):
    command = [sys.executable] if not args.wsl_distro else ['wsl', '-d', args.wsl_distro, '--exec', 'python3']
    generated = subprocess.run([*command, args.generator_script, 'valid', 'localhost'], capture_output=True, timeout=30, check=True)
    material = json.loads(generated.stdout)  # contains a newly generated SYNTHETIC key; never log this value
    with tempfile.TemporaryDirectory(prefix='tls-stream-', dir=args.scratch) as directory:
        root = Path(directory)
        cert, key = root / 'server.pem', root / 'server.key'
        cert.write_text(material['cert'], encoding='ascii')
        key.write_text(material['key'], encoding='ascii')
        ca, crl = base64.b64decode(material['root']), base64.b64decode(material['crl'])
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        try:
            context.load_cert_chain(cert, key)
        finally:
            key.unlink(missing_ok=True)
        del material, generated
        for mode in ([args.case] if args.case else ('echo', 'canonical', 'tls12', 'untrusted', 'truncate')):
            context.maximum_version = ssl.TLSVersion.TLSv1_2 if mode == 'tls12' else ssl.TLSVersion.MAXIMUM_SUPPORTED
            listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            listener.bind(('127.0.0.1', 0))
            listener.listen(1)
            listener.settimeout(10)
            port = listener.getsockname()[1]
            done = threading.Event()
            errors = []
            applications = bytearray()
            submitted = []
            def serve():
                try:
                    with listener:
                        raw, _ = listener.accept()
                        with raw:
                            raw.settimeout(10)
                            try:
                                with context.wrap_socket(raw, server_side=True) as peer:
                                    pending = bytearray()
                                    while not done.is_set():
                                        data = peer.recv(16384)
                                        if not data:
                                            break
                                        applications.extend(data)
                                        if len(applications) > 262144:
                                            raise RuntimeError('synthetic peer input budget exceeded')
                                        if mode == 'echo':
                                            peer.sendall(data)
                                            continue
                                        pending.extend(data)
                                        while b'\n' in pending:
                                            line, _, rest = pending.partition(b'\n')
                                            pending[:] = rest
                                            message = json.loads(line)
                                            if message.get('method') == 'mining.authorize':
                                                peer.sendall(b'{"id":1,"result":true,"error":null}\n' + notification())
                                                if mode == 'truncate':
                                                    peer.shutdown(socket.SHUT_RDWR)
                                                    return
                                            elif message.get('method') == 'mining.submit':
                                                submitted.append(message['id'])
                                                peer.sendall(json.dumps({'id': message['id'], 'result': True, 'error': None}, separators=(',', ':')).encode() + b'\n')
                            except (ssl.SSLError, ConnectionError):
                                pass  # deliberate native abort or negative trust; never an application acknowledgement
                except Exception as error:
                    errors.append(type(error).__name__)
            thread = threading.Thread(target=serve, daemon=True)
            thread.start()
            private = ([ca], [], [crl]) if os.name == 'nt' and mode != 'untrusted' else None
            pem = ssl.DER_cert_to_PEM_cert(ca).encode('ascii') if os.name != 'nt' and mode != 'untrusted' else b''
            fallback_listener = None
            probe_mode = [] if mode == 'echo' else ['canonical']
            if args.fallback_refusal:
                assert mode == 'untrusted', 'fallback refusal requires the ordinary untrusted case'
                fallback_listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                fallback_listener.bind(('127.0.0.1', 0))
                fallback_listener.listen(1)
                fallback_listener.settimeout(0.1)
                probe_mode = ['fallback', f'stratum+tcp://localhost:{fallback_listener.getsockname()[1]}']
            try:
                process = subprocess.run([str(args.probe), str(args.worker), *probe_mode],
                                         input=packet('localhost', port, 5000, pem, private), capture_output=True, timeout=25, check=True)
                result = json.loads(process.stdout)
                if fallback_listener:
                    try:
                        unexpected, _ = fallback_listener.accept()
                    except socket.timeout:
                        pass
                    else:
                        unexpected.close()
                        raise AssertionError('verification failure attempted plaintext fallback')
                    assert result['plaintext'] == 0, result
            finally:
                done.set()
                thread.join(timeout=11)
                listener.close()
                if fallback_listener:
                    fallback_listener.close()
            assert not thread.is_alive() and not errors, (mode, errors, result, len(applications), len(submitted))
            if mode in ('untrusted', 'truncate'):
                assert not result['ok'] and not submitted, (mode, result, len(submitted))
                if mode == 'untrusted':
                    assert result['tls'] == 4 and not applications, (mode, result)
            else:
                assert result['ok'] == 1 and result['tls'] == 0, (mode, result)
                if mode == 'echo':
                    assert applications == bytes(i % 256 for i in range(32779))
                else:
                    assert len(submitted) == 1
            print('retained native TLS loopback:', mode, 'passed', flush=True)


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('synthetic assertions require non-optimized Python')
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--worker', type=Path, required=True)
    parser.add_argument('--generator-script', required=True)
    parser.add_argument('--wsl-distro')
    parser.add_argument('--scratch', type=Path, required=True)
    parser.add_argument('--case', choices=('echo', 'canonical', 'tls12', 'untrusted', 'truncate'))
    parser.add_argument('--fallback-refusal', action='store_true')
    args = parser.parse_args()
    args.scratch.mkdir(parents=True, exist_ok=True)
    main(args)
