"""Explicit native Schannel versus Python/OpenSSL MemoryBIO, no sockets, roots, user keys or live endpoints.

Needs a separately built probe, an approved private scratch directory, and a supplied cryptography generator interpreter.
The generator JSON contains a newly created SYNTHETIC key: capture it, never log it, unlink temporary key files after load.
"""
import argparse
import base64
import json
from pathlib import Path
import queue
import ssl
import subprocess
import sys
import tempfile
import threading


class Probe:
    def __init__(self, exe, root, crl, name):
        self.process = subprocess.Popen([str(exe), str(root), str(crl), name], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL, text=True, bufsize=1)
        self.lines = queue.Queue()
        def read_lines():
            for line in self.process.stdout:
                self.lines.put(line)
        self.thread = threading.Thread(target=read_lines, daemon=True)
        self.thread.start()
        try:
            self.initial = self.answer()
        except Exception:
            self.close()
            raise

    def answer(self):
        try:
            value = self.lines.get(timeout=10)
        except queue.Empty:
            raise RuntimeError('owned synthetic probe did not respond') from None
        if len(value) > 300000:
            raise RuntimeError('oversize probe reply')
        return json.loads(value)

    def cmd(self, text):
        self.process.stdin.write(text + '\n')
        self.process.stdin.flush()
        return self.answer()

    def close(self):
        try:
            if self.process.poll() is None:
                self.process.stdin.write('Q\n')
                self.process.stdin.flush()
                self.process.wait(timeout=5)
        finally:
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait(timeout=5)
            self.process.stdin.close()
            self.thread.join(timeout=5)
            self.process.stdout.close()


def scenario(args, mode, maximum=None, fragmented=False, reference='fixture.test', termination='cancel', backpressure=False):
    command = ([sys.executable] if not args.wsl_distro else ['wsl', '-d', args.wsl_distro, '--exec', 'python3'])
    generated = subprocess.run([*command, args.generator_script, mode], capture_output=True, check=True, timeout=30)
    # Do not print generated.stdout/stderr or material. They may contain the test-owned ephemeral key.
    material = json.loads(generated.stdout)
    with tempfile.TemporaryDirectory(prefix='schannel-memory-', dir=args.scratch) as directory:
        root = Path(directory)
        ca = root / 'root.der'
        crl = root / 'root.crl'
        cert = root / 'server.pem'
        key = root / 'server.key'
        ca.write_bytes(base64.b64decode(material['root']))
        crl.write_bytes(base64.b64decode(material['crl']))
        cert.write_text(material['cert'], encoding='ascii')
        key.write_text(material['key'], encoding='ascii')
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        if maximum:
            context.maximum_version = maximum
        if mode == 'clientauth':
            context.verify_mode = ssl.CERT_REQUIRED
            context.load_verify_locations(cadata=ssl.DER_cert_to_PEM_cert(ca.read_bytes()))
        try:
            context.load_cert_chain(cert, key)
        finally:
            key.unlink(missing_ok=True)
        del material, generated
        incoming, outgoing = ssl.MemoryBIO(), ssl.MemoryBIO()
        server = context.wrap_bio(incoming, outgoing, server_side=True)
        probe = Probe(args.probe, ca, '-' if mode == 'missing' else crl, reference)
        done = False
        server_failure = False
        application = bytearray()
        states = set()
        control_turns = 0

        def exchange(response):
            nonlocal done, server_failure
            states.add(response['state'])
            cipher = bytes.fromhex(response['cipher'])
            if cipher:
                incoming.write(cipher)
            if not done and not server_failure:
                try:
                    server.do_handshake()
                    done = True
                except (ssl.SSLWantReadError, ssl.SSLWantWriteError):
                    pass
                except ssl.SSLError as error:
                    if mode != 'clientauth' or error.reason != 'PEER_DID_NOT_RETURN_A_CERTIFICATE':
                        raise
                    server_failure = True
            if done:
                for _ in range(32):
                    try:
                        data = server.read(65536)
                        if not data:
                            break
                        application.extend(data)
                        if len(application) > 1000000:
                            raise RuntimeError('synthetic application budget exceeded')
                        if server.write(data) != len(data):
                            raise RuntimeError('partial synthetic echo')
                    except (ssl.SSLWantReadError, ssl.SSLWantWriteError):
                        break
            while outgoing.pending:
                probe.cmd('I ' + outgoing.read(16384).hex())

        try:
            if probe.initial['status'] == 3:
                raise RuntimeError('synthetic private mode failed before handshake')
            if mode == 'deadline':
                probe.cmd('N 5000')
                r = probe.cmd('H')
                assert r['state'] == 4 and r['error'] == 9 and not r['cipher']
                return
            if fragmented:
                probe.cmd('S 0')
                for _ in range(5):
                    r = probe.cmd('H')
                    assert r['status'] == 1 and r['interest'] == 2 and r['state'] == 1 and not r['cipher']
                probe.cmd('S 7')
                probe.cmd('F 17')
            r = probe.initial
            for _ in range(20000):
                r = probe.cmd('R' if mode == 'clientauth' and r['state'] == 2 else 'H')
                exchange(r)
                if r['state'] == 4 or (r['state'] == 2 and done):
                    break
            else:
                raise RuntimeError('native handshake did not settle within its call budget')
            valid = mode in ('valid', 'deadline') and reference == 'fixture.test'
            if not valid:
                if mode == 'clientauth':
                    assert r['state'] == 4 and not application and (r['error'] == 2 or (server_failure and r['error'] in (4, 5)))
                else:
                    assert r['state'] == 4 and r['error'] == 4 and not application, (mode, r['state'], r['error'])
                return
            assert r['state'] == 2 and done and not application, (mode, r['state'], r['error'])
            message = b'synthetic-authorize\nsynthetic-submit\n' if fragmented or termination != 'cancel' else (b'exact-memory-records\n' * 1900)
            sent = 0
            first = True
            if backpressure:
                probe.cmd('S 0')
                r = probe.cmd('W ' + message.hex())
                first = False
                assert r['status'] == 1 and not r['bytes'] and not r['cipher'] and not application
                for _ in range(5):
                    r = probe.cmd('W')
                    assert r['status'] == 1 and r['interest'] == 2 and not r['bytes'] and not r['cipher']
                probe.cmd('S 7')
            for _ in range(20000):
                r = probe.cmd('W ' + message.hex() if first else 'W')
                first = False
                exchange(r)
                if r['status'] == 0:
                    sent += r['bytes']
                assert r['state'] != 4, (mode, 'send', r['error'])
                if sent == len(message):
                    break
            else:
                raise RuntimeError('native send did not settle')
            received = bytearray()
            for _ in range(20000):
                r = probe.cmd('R')
                if r['state'] == 1:
                    control_turns += 1
                    if termination == 'control-deadline':
                        probe.cmd('N 11000')
                        expired = probe.cmd('R')
                        assert expired['state'] == 4 and expired['error'] == 9 and not expired['plain']
                        return
                exchange(r)
                assert r['state'] != 4, (mode, 'receive', r['error'])
                received.extend(bytes.fromhex(r['plain']))
                if len(received) == len(message):
                    break
            assert application == message and received == message, (mode, len(application), len(received))
            if mode == 'valid' and reference == 'fixture.test':
                print('native record scope:', server.version(), 'post-handshake control turns', control_turns, flush=True)
                if server.version() == 'TLSv1.3':
                    assert control_turns > 0
            if termination == 'close':
                try:
                    server.unwrap()
                except (ssl.SSLWantReadError, ssl.SSLWantWriteError):
                    pass
                while outgoing.pending:
                    probe.cmd('I ' + outgoing.read(16384).hex())
                for _ in range(128):
                    r = probe.cmd('R')
                    if r['status'] in (2, 3):
                        break
                assert r['status'] == 2 and r['state'] == 3 and not r['plain'], ('clean-close', r['status'], r['state'], r['error'], len(r['plain']))
            elif termination == 'truncate':
                probe.cmd('E')
                for _ in range(128):
                    r = probe.cmd('R')
                    assert r['status'] != 2
                    if r['state'] == 4:
                        break
                assert r['state'] == 4 and r['error'] == 5 and not r['plain']
            else:
                assert termination != 'control-deadline', 'required TLSv1.3 control phase was not reached'
                probe.cmd('C')
                r = probe.cmd('R')
                assert r['state'] == 4 and r['error'] == 8 and not r['plain']
        finally:
            probe.close()


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('synthetic fixture assertions require non-optimized Python')
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', required=True, type=Path)
    parser.add_argument('--scratch', required=True, type=Path)
    parser.add_argument('--generator-script', required=True)
    parser.add_argument('--wsl-distro')
    parser.add_argument('--case', choices=('close', 'truncate', 'control-deadline', 'backpressure'))
    args = parser.parse_args()
    args.scratch.mkdir(parents=True, exist_ok=True)
    if args.case:
        scenario(args, 'valid', termination=args.case if args.case != 'backpressure' else 'cancel', backpressure=args.case == 'backpressure')
        print('Schannel memory:', args.case, 'passed', flush=True)
        sys.exit(0)
    for mode in ('missing', 'stale', 'revoked', 'expired', 'future', 'wrongusage', 'wrongsan', 'nosan', 'wrongroot', 'clientauth', 'deadline', 'valid'):
        scenario(args, mode)
        print('Schannel memory:', mode, 'passed', flush=True)
    scenario(args, 'valid', maximum=ssl.TLSVersion.TLSv1_2, fragmented=True)
    scenario(args, 'valid', reference='wrong-fixture.test')
    scenario(args, 'valid', termination='close')
    scenario(args, 'valid', termination='truncate')
    scenario(args, 'valid', termination='control-deadline')
    scenario(args, 'valid', backpressure=True)
    print('Schannel memory: 18 scenarios passed; no sockets or OS-store install', flush=True)
