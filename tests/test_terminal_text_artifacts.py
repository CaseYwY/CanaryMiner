"""Default-off/source/import boundaries only; not dependency authentication or terminal qualification."""
import argparse
import hashlib
from pathlib import Path
import re


def main(args):
    root = Path(__file__).resolve().parents[1]
    cmake = (root / 'cmake/CanaryTerminalText.cmake').read_text(encoding='utf-8')
    assert re.search(r'option\(CANARY_ENABLE_TERMINAL_ICU\s+"[^"]+"\s+OFF\)', cmake)
    assert all(marker not in cmake for marker in ('FetchContent', 'file(DOWNLOAD', 'ExternalProject', 'execute_process'))
    main_cmake = (root / 'CMakeLists.txt').read_text(encoding='utf-8')
    assert 'target_link_libraries(canaryminer PRIVATE canary_terminal_text)' not in main_cmake
    assert 'canary_localized_format' not in main_cmake
    for source in ('src/core/miner.cu', 'src/product/dashboard.h', 'src/product/supervisor.h', 'src/product/settings.h'):
        text = (root / source).read_text(encoding='utf-8')
        assert all(marker not in text for marker in ('formatPresentationNumber', 'layoutTerminalText', 'wrapTerminalText', 'padTerminalText', 'terminalGraphemeBoundaries', 'ubidi_', 'ubrk_'))
    assert hashlib.sha256((root / 'LICENSES/ICU-79.1.txt').read_bytes()).hexdigest() == 'e09827667e0bb3c808b2a463548ba814a5e6249b67353234d70c8dfc2bcc417d'
    if args.disabled_library:
        data = args.disabled_library.read_bytes()
        assert all(marker not in data for marker in (b'ubrk_open', b'ubidi_setPara', b'u_charMirror', b'u_stringHasBinaryProperty', b'icuuc79.dll'))
    if args.disabled_format:
        data = args.disabled_format.read_bytes()
        assert all(marker not in data for marker in (b'unumf_', b'uplrules_', b'icuuc79.dll', b'icuin79.dll'))
    print('terminal text artifacts: ICU opt-in OFF, no automatic downloads or ordinary activation, exact notice retained')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    parser = argparse.ArgumentParser()
    parser.add_argument('--disabled-library', type=Path)
    parser.add_argument('--disabled-format', type=Path)
    main(parser.parse_args())
