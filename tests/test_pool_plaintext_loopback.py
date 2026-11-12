"""Explicit plaintext helper against owned localhost peers. No public pool, GPU or trust-store change."""
import argparse
import json
from pathlib import Path
import socket
import subprocess
import threading

from test_pool_tls_check_process import packet
from test_pool_tls_stream_loopback import notification


def main(args):
    for mode in ('echo', 'canonical', 'eof', 'cancel', 'refused'):
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.bind(('127.0.0.1', 0))
        port = listener.getsockname()[1]
        applications = bytearray()
        submitted = []
        errors = []
        done = threading.Event()

        def serve():
            try:
                listener.settimeout(10)
                peer, _ = listener.accept()
                with peer:
                    peer.settimeout(10)
                    if mode == 'eof':
                        peer.shutdown(socket.SHUT_WR)
                        return
                    if mode == 'cancel':
                        assert peer.recv(1) == b''
                        return
                    pending = bytearray()
                    while not done.is_set():
                        data = peer.recv(8191)
                        if not data:
                            return
                        applications.extend(data)
                        assert len(applications) <= 262144
                        if mode == 'echo':
                            for start in range(0, len(data), 257):
                                peer.sendall(data[start:start + 257])
                            continue
                        pending.extend(data)
                        while b'\n' in pending:
                            line, _, rest = pending.partition(b'\n')
                            pending[:] = rest
                            message = json.loads(line)
                            if message.get('method') == 'mining.authorize':
                                peer.sendall(b'{"id":1,"result":true,"error":null}\n' + notification())
                            elif message.get('method') == 'mining.submit':
                                submitted.append(message['id'])
                                peer.sendall(json.dumps({'id': message['id'], 'result': True, 'error': None}, separators=(',', ':')).encode() + b'\n')
            except ConnectionResetError:
                pass  # the owned helper is forcibly retired; exact bytes/acknowledgements are asserted below
            except Exception as error:
                errors.append(type(error).__name__)

        thread = None
        try:
            # Keep the refused port bound but not listening, so it cannot be reassigned to an unrelated peer.
            if mode != 'refused':
                listener.listen(1)
                thread = threading.Thread(target=serve, daemon=True)
                thread.start()
            result = subprocess.run([str(args.probe), str(args.worker), 'plaintext-' + mode],
                                    input=packet('localhost', port, 5000), capture_output=True, timeout=25, check=True)
            result = json.loads(result.stdout)
        finally:
            done.set()
            if thread:
                thread.join(timeout=11)
            listener.close()
        assert not errors and (not thread or not thread.is_alive()), (mode, errors)
        assert result['ok'] == 1, (mode, result)
        if mode == 'refused':
            assert result['failure'] == 7 and result['stage'] == 2 and not applications, result
        else:
            assert result['failure'] == -1 and result['stage'] == -1, result
            assert result['tls'] == (8 if mode == 'cancel' else 0), result
        if mode == 'echo':
            assert applications == bytes(i % 256 for i in range(32779))
        elif mode == 'canonical':
            assert len(submitted) == 1
        else:
            assert not applications and not submitted
        print('retained native plaintext loopback:', mode, 'passed', flush=True)


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--worker', type=Path, required=True)
    main(parser.parse_args())
