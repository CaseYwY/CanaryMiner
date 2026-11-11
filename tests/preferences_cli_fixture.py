"""Build the genuine fixed CPU sibling for isolated GCC CLI fixtures; never a miner or device backend."""
from pathlib import Path
import subprocess


def build_preferences_helper(directory, compiler='g++'):
    root = Path(__file__).resolve().parents[1]
    helper = Path(directory) / 'canary_preferences_worker'
    units = ('preferences_worker', 'preferences_service', 'preferences_protocol', 'duplex_task_process',
             'local_task_process', 'exact_decimal', 'manual_market', 'electricity', 'civil_time', 'tariff_declaration')
    command = [compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-pthread',
               *('src/product/' + name + '.cpp' for name in units), '-o', str(helper)]
    result = subprocess.run(command, cwd=root, capture_output=True, timeout=90)
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors='replace'))
    return helper
