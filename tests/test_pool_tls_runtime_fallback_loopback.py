"""Owned localhost native-connect fallback, ordinary source and real helper; no public peer or GPU."""
import argparse
import json
from pathlib import Path
import socket
import subprocess
import threading
from test_pool_tls_check_process import packet
from test_pool_tls_stream_loopback import notification


def main(args):
    checks = 0
    for mode in ('strict', 'consented', 'handshake-error'):
        # Bind and retain the unavailable TLS port so another process cannot acquire it.
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as unavailable, \
                socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            unavailable.bind(('127.0.0.1', 0))
            tls_port = unavailable.getsockname()[1]
            listener.bind(('127.0.0.1', 0))
            listener.listen(1)
            listener.settimeout(0.1)
            plaintext_port = listener.getsockname()[1]
            if mode == 'handshake-error':
                unavailable.listen(1)
                unavailable.settimeout(0.1)
            done = threading.Event()
            messages, errors, connections = [], [], []

            def plaintext_peer():
                try:
                    while not done.is_set():
                        try:
                            peer, _ = listener.accept()
                            break
                        except socket.timeout:
                            continue
                    else:
                        return
                    connections.append('plaintext')
                    with peer:
                        peer.settimeout(2)
                        pending = bytearray()
                        while not done.is_set():
                            block = peer.recv(16384)
                            if not block:
                                return
                            pending.extend(block)
                            if len(pending) > 65536:
                                raise RuntimeError('fixture input exceeded its budget')
                            while b'\n' in pending:
                                line, _, rest = pending.partition(b'\n')
                                pending[:] = rest
                                message = json.loads(line)
                                messages.append(message)
                                if message.get('method') == 'mining.authorize':
                                    peer.sendall(b'{"id":1,"result":true,"error":null}\n' + notification())
                                elif message.get('method') == 'mining.submit':
                                    peer.sendall(json.dumps(dict(id=message['id'], result=True, error=None)).encode() + b'\n')
                except (ConnectionError, socket.timeout):
                    pass
                except Exception as error:
                    errors.append(type(error).__name__)

            def invalid_tls_peer():
                try:
                    while not done.is_set():
                        try:
                            peer, _ = unavailable.accept()
                            break
                        except socket.timeout:
                            continue
                    else:
                        return
                    with peer:
                        peer.settimeout(2)
                        assert peer.recv(16384), 'expected a ClientHello'
                        connections.append('handshake')
                        peer.sendall(b'not a TLS record\n')
                except (ConnectionError, socket.timeout):
                    pass
                except Exception as error:
                    errors.append(type(error).__name__)

            threads = [threading.Thread(target=plaintext_peer)]
            if mode == 'handshake-error':
                threads.append(threading.Thread(target=invalid_tls_peer))
            for thread in threads:
                thread.start()
            arguments = ['canonical'] if mode == 'strict' else ['fallback', f'stratum+tcp://localhost:{plaintext_port}']
            try:
                process = subprocess.run([str(args.probe), str(args.worker), *arguments],
                                         input=packet('localhost', tls_port, 5000), capture_output=True,
                                         timeout=20, check=True)
                result = json.loads(process.stdout)
            finally:
                done.set()
                for thread in threads:
                    thread.join(timeout=3)
            assert not errors and all(not thread.is_alive() for thread in threads), (mode, errors)
            methods = [message.get('method') for message in messages]
            if mode == 'consented':
                assert result['ok'] == 1 and result['plaintext'] == 1, (mode, result)
                assert connections == ['plaintext'] and methods == ['mining.authorize', 'mining.submit'], (mode, methods)
            else:
                assert result['ok'] == 0 and result['plaintext'] == 0 and not messages, (mode, result, methods)
                assert 'plaintext' not in connections, mode
                if mode == 'handshake-error':
                    assert connections == ['handshake'], connections
            checks += 1
    print(f'ordinary native fallback loopback: {checks} strict, consented and handshake-error cases passed')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--worker', type=Path, required=True)
    main(parser.parse_args())
