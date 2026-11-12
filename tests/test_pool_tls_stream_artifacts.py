"""Structural/default-off and compiled separation checks; not packaging or native-stall qualification."""
import argparse
from pathlib import Path
import re


def main(args):
    root = Path(__file__).resolve().parent.parent
    cmake = (root / 'cmake/CanaryPoolTls.cmake').read_text(encoding='utf-8')
    tests = (root / 'tests/CMakeLists.txt').read_text(encoding='utf-8')
    for option in ('CANARY_ENABLE_POOL_TLS_RUNTIME', 'CANARY_ENABLE_POOL_TLS_OPENSSL', 'CANARY_ENABLE_POOL_TLS_PRIVATE_TRUST', 'CANARY_ENABLE_POOL_TLS_SCHANNEL',
                   'CANARY_ENABLE_POOL_TLS_CHECK', 'CANARY_ENABLE_POOL_TLS_STREAM'):
        assert re.search(r'option\(' + option + r'\s+"[^"]+"\s+OFF\)', cmake), option
    for option in ('CANARY_SCHANNEL_MEMORY_TESTS', 'CANARY_POOL_TLS_CHECK_LOOPBACK_TESTS', 'CANARY_POOL_TLS_STREAM_LOOPBACK_TESTS'):
        assert re.search(r'option\(' + option + r'\s+"[^"]+"\s+OFF\)', tests), option
    for source in ('src/core/miner.cu', 'src/core/run.cpp', 'src/product/supervisor.h'):
        text = (root / source).read_text(encoding='utf-8')
        # Ordinary callers may own the strict runtime wrapper. Native crypto/socket setup
        # and the single-connection IPC implementation remain behind that owner.
        for marker in ('PoolTlsProcessTransport', 'openPoolTlsConnection', 'canary_pool_tls_process'):
            assert marker not in text, (source, marker)
    forbidden = (b'lost-submit.test', b'lost-write.test', b'echo.test', b'stall-open.test', b'verify-fail.test', b'synthetic-proof', b'DEBUG-P12F')
    artifacts = (args.parent_library, args.service_library, args.duplex_library) + ((args.runtime_library,) if args.runtime_library else ())
    for artifact in artifacts:
        data = artifact.read_bytes()
        assert all(marker not in data for marker in forbidden), 'fixture marker in ordinary library'
    for artifact in (args.parent_library,) + ((args.runtime_library,) if args.runtime_library else ()):
        data = artifact.read_bytes()
        assert all(symbol not in data for symbol in (b'InitializeSecurityContextW', b'AcquireCredentialsHandleW', b'getaddrinfo', b'SSL_connect'))
    if args.disabled_native_library:
        data = args.disabled_native_library.read_bytes()
        assert all(symbol not in data for symbol in (b'InitializeSecurityContextW', b'AcquireCredentialsHandleW', b'getaddrinfo', b'WSAStartup', b'SSL_connect'))
    data = args.ordinary_artifact.read_bytes()
    assert all(marker not in data for marker in (b'PoolTlsProcessTransport', b'CMTLSC1', b'runPoolTlsStreamService'))
    print('persistent TLS artifacts: runtime/development opt-ins OFF by default; no native parent IO or fixture seams; disabled artifact remains isolated')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    parser = argparse.ArgumentParser()
    parser.add_argument('--parent-library', type=Path, required=True)
    parser.add_argument('--service-library', type=Path, required=True)
    parser.add_argument('--duplex-library', type=Path, required=True)
    parser.add_argument('--runtime-library', type=Path)
    parser.add_argument('--ordinary-artifact', type=Path, required=True)
    parser.add_argument('--disabled-native-library', type=Path)
    main(parser.parse_args())
