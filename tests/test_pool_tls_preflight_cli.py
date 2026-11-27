"""The standalone TLS check uses a fixed sibling and never enters normal miner setup."""
import ctypes
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import sys
import tempfile


def main(cli, mode, fixture, native=None):
    available = mode == 'on'
    assert available == (native is not None)
    suffix = '.exe' if os.name == 'nt' else ''
    worker_name = 'canary_pool_tls_stream_worker' + suffix
    with tempfile.TemporaryDirectory(prefix='canary-tls-preflight-') as directory:
        root = Path(directory)
        bundle = root / 'Miner 安装'
        bundle.mkdir()
        cli_path = bundle / ('canary-miner' + suffix)
        shutil.copy2(cli, cli_path)
        foreign_worker = root / worker_name
        shutil.copy2(fixture, foreign_worker)
        bundled_worker = bundle / worker_name

        # Ordinary startup reads preferences under state/ by default.
        state = root / 'state'
        state.mkdir()
        inputs = (root / 'config.json', state / 'pool-security-preferences.json',
                  state / 'runtime-preferences.json')
        for path in inputs:
            path.write_bytes(b'private malformed fixture')
        env = {**os.environ, 'PATH': str(root) + os.pathsep + os.environ.get('PATH', '')}

        def run(*args, code, message=b'', public_success=False):
            result = subprocess.run([cli_path, 'pool-tls-check', *args], cwd=root, env=env,
                                    capture_output=True, timeout=16)
            output = result.stdout if code == 0 else result.stderr
            assert result.returncode == code and message in output, result
            assert bool(result.stdout) == (code == 0), result
            assert (b'certificate and hostname verified' in result.stdout) == public_success
            assert not any(secret in output for secret in (b'private malformed', b'not-a-password',
                           b'echo.test', b'FORBIDDEN_DEVICE_SETUP', b'kryptex.network')), result
            return result

        help_result = run('--help', code=0, message=b'--pool stratum+ssl://')
        assert b'--pool kryptex-hk-tls' in help_result.stdout
        for options in ([], ['--pool'], ['--help', '--pool', 'stratum+ssl://echo.test:443'],
                        ['--pool', 'stratum+tcp://echo.test:443'], ['--pool', 'kryptex-hk'],
                        ['--pool', 'herominers'], ['--pool', 'f2pool'], ['--pool', 'KRYPTEX-HK-TLS'],
                        ['--pool', 'kryptex-hk-tl\uff53'], ['--pool', 'unknown-tls'],
                        ['--pool', 'kryptex-hk-tls:8048'], ['--pool', 'kryptex-hk-tls', '--pool', 'kryptex-tls'],
                        ['--pool', 'stratum+ssl://127.0.0.1:443'],
                        ['--pool', 'stratum+ssl://0X7f.0.0.1:443'],
                        ['--pool', 'stratum+ssl://0x7f000001:443'],
                        ['--pool', 'stratum+ssl://[::1]:443'], ['--pool', 'stratum+ssl://echo.test:0443'],
                        ['--pool', 'stratum+ssl://echo.test:443/path'],
                        ['--pool', 'stratum+ssl://not-a-password@echo.test:443'],
                        ['--pool', 'stratum+ssl://echo.test:443', '--no-donation'],
                        ['--pool', 'stratum+ssl://echo.test:443', '--config', str(inputs[0])],
                        ['--pool', 'stratum+ssl://echo.test:443', '--wallet', 'not-a-password']):
            run(*options, code=2, message=b'pool-tls-check --help')
        url = 'stratum+ssl://echo.test:443'
        if not available:
            shutil.copy2(foreign_worker, bundled_worker)
            run('--pool', url, code=3, message=b'runtime is unavailable')
            run('--pool', 'kryptex-hk-tls', code=3, message=b'runtime is unavailable')
            run('--pool', 'herominers-tls', code=3, message=b'runtime is unavailable')
            return

        # There is a genuine fixture in both the working directory and PATH.
        run('--pool', url, code=3, message=b'transport')
        shutil.copy2(foreign_worker, bundled_worker)
        run('--pool', 'stratum+ssl://ECHO.test:443', code=0, message=b'Connection closed.', public_success=True)
        targets = (
            ('kryptex-tls', 'prl-us.kryptex.network', 8048),
            ('kryptex-hk-tls', 'prl-hk.kryptex.network', 8048),
            ('kryptex-global-tls', 'prl.kryptex.network', 8048),
            ('kryptex-eu-tls', 'prl-eu.kryptex.network', 8048),
            ('kryptex-br-tls', 'prl-br.kryptex.network', 8048),
            ('kryptex-sg-tls', 'prl-sg.kryptex.network', 8048),
            ('kryptex-ru-tls', 'prl-ru.kryptex.network', 8048),
            ('kryptex-ae-tls', 'prl-ae.kryptex.network', 8048),
            ('herominers-tls', 'us2.pearl.herominers.com', 1200),
        )
        marker = bundle / 'preflight-target'
        for alias, host, port in targets:
            marker.write_bytes(b'')
            run('--pool', alias, code=0, message=b'Connection closed.', public_success=True)
            assert marker.read_text(encoding='ascii') == f'{host} {port} tls\n'
        marker.write_bytes(b'')
        run('--pool', 'kryptex-hk-tl\uff53', code=2, message=b'pool-tls-check --help')
        assert not marker.read_bytes()
        marker.unlink()
        for host, reason in (('dns-fail.test', b'dns'), ('connect-fail.test', b'connect'),
                             ('verify-fail.test', b'verification'), ('wrong-mode.test', b'protocol')):
            run('--pool', f'stratum+ssl://{host}:443', code=3, message=b'pool-tls-check: ' + reason)

        shutil.copy2(native, bundled_worker)
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as closed:
            closed.bind(('127.0.0.1', 0))
            port = closed.getsockname()[1]
            run('--pool', f'stratum+ssl://localhost:{port}', code=3, message=b'pool-tls-check: connect')

        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.bind(('127.0.0.1', 0))
            listener.listen(1)
            listener.settimeout(5)
            port = listener.getsockname()[1]
            can_signal = os.name != 'nt' or ctypes.windll.kernel32.GetConsoleCP() != 0
            flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == 'nt' and can_signal else 0
            with subprocess.Popen([cli_path, 'pool-tls-check', '--pool', f'stratum+ssl://localhost:{port}'],
                                  cwd=root, env=env, creationflags=flags, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE) as process:
                try:
                    peer, _ = listener.accept()
                    with peer:
                        peer.settimeout(4)
                        with peer.makefile('rb') as wire:
                            header = wire.read(5)
                            assert len(header) == 5 and header[:2] == b'\x16\x03', header
                            size = int.from_bytes(header[3:], 'big')
                            assert 0 < size <= 16384 and len(wire.read(size)) == size
                        if can_signal:
                            process.send_signal(signal.CTRL_BREAK_EVENT if os.name == 'nt' else signal.SIGINT)
                        else:
                            peer.sendall(b'HTTP/1.0 400 Bad Request\r\n\r\n')
                        stdout, stderr = process.communicate(timeout=6)
                        assert not stdout, stdout
                        if can_signal:
                            assert process.returncode == 130 and b'pool-tls-check: cancelled' in stderr, stderr
                            try:
                                assert peer.recv(1) == b'', 'the installed worker must close its socket on cancel'
                            except ConnectionResetError:
                                pass  # Abortive socket shutdown also proves the worker lost the connection.
                        else:
                            assert process.returncode == 3 and b'pool-tls-check:' in stderr, stderr
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait(timeout=5)
            listener.settimeout(0.1)
            try:
                unexpected, _ = listener.accept()
            except socket.timeout:
                pass
            else:
                unexpected.close()
                raise AssertionError('the check opened a second connection')

        assert set(root.rglob('*')) == {bundle, state, cli_path, foreign_worker, bundled_worker, *inputs}
        for path in inputs:
            assert path.read_bytes() == b'private malformed fixture'


if __name__ == '__main__':
    main(*sys.argv[1:])
