"""Real inert CLI shape admission tests; no device or backend execution.

Usage: python tests/test_pearl_shape_cli_host.py SHAPE_COMPILED_OUT SHAPE_CAPABLE
       [SHAPE_ENABLED_BACKEND_MISSING]

All binaries must use actual miner/registry parser sources and test_pearl_cli_stubs.
Both mandatory binaries enable SM120 native evidence; only the second enables
CANARY_PEARL_ENABLE_SHAPE_EXPERIMENT=1 and PEARL_CLI_SHAPE_CAPABLE_STUB=1.
The optional third enables the shape compile gate but omits the backend stub
capability. Guard compilation is unnecessary: no case supplies owned guard FDs.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

PROFILE = 'shape4096x65536-v1'
SHAPE = '4096x65536'
MARKER = b'FORBIDDEN_DEVICE_SETUP'


def main():
    parser = argparse.ArgumentParser(allow_abbrev=False)
    parser.add_argument('compiled_out', type=Path)
    parser.add_argument('shape_capable', type=Path)
    parser.add_argument('backend_missing', type=Path, nargs='?')
    args = parser.parse_args()
    binaries = [('compiled_out', args.compiled_out.resolve()),
                ('shape_capable', args.shape_capable.resolve())]
    if args.backend_missing is not None:
        binaries.append(('backend_missing', args.backend_missing.resolve()))
    for label, binary in binaries:
        if not binary.is_file():
            raise ValueError('missing inert CLI binary: ' + label)
    inherited = {k: v for k, v in os.environ.items() if not k.startswith('CANARY_PEARL_')}
    checks = 0
    for label, binary in binaries:
        for mode in ('quota', 'duration'):
            baseline = dict(inherited, CANARY_PEARL_LOCAL_RETIREMENTS='1',
                CANARY_PEARL_EVIDENCE_RUN_ID='1' * 32,
                CANARY_PEARL_NATIVE_EVIDENCE='sm120-generic-v1')
            if mode == 'quota':
                baseline['CANARY_PEARL_ACCEPTED_QUOTA'] = '2'
            else:
                baseline.update(CANARY_PEARL_SUSTAINED_MODE='private-v1',
                                CANARY_PEARL_SUSTAINED_SECONDS='1')
            # None removes an environment key; '' deliberately retains an empty value.
            cases = [('ordinary_generic_no_shape', {}, True),
                     ('exact_pair_no_owned_guard', {}, False),
                     ('profile_missing_selector', {'CANARY_PEARL_SHAPE': None}, False),
                     ('selector_without_profile', {'CANARY_PEARL_SHAPE_EXPERIMENT': None}, False)]
            for key in ('CANARY_PEARL_SHAPE_EXPERIMENT', 'CANARY_PEARL_SHAPE'):
                valid = PROFILE if key.endswith('EXPERIMENT') else SHAPE
                for suffix, value in [('empty', ''), ('unknown', 'invalid'),
                                      ('trailing', valid + 'x'), ('space', valid + ' '),
                                      ('newline', valid + '\n')]:
                    cases.append((key + '_' + suffix, {key: value}, False))
            cases.extend([
                ('missing_native_mode', {'CANARY_PEARL_NATIVE_EVIDENCE': None}, False),
                ('empty_native_mode', {'CANARY_PEARL_NATIVE_EVIDENCE': ''}, False),
                ('wrong_native_mode', {'CANARY_PEARL_NATIVE_EVIDENCE': 'retired-generic-v1'}, False),
                ('unknown_native_mode', {'CANARY_PEARL_NATIVE_EVIDENCE': 'sm120-generic-v1x'}, False),
                ('no_stop', {'CANARY_PEARL_ACCEPTED_QUOTA': None, 'CANARY_PEARL_SUSTAINED_MODE': None,
                             'CANARY_PEARL_SUSTAINED_SECONDS': None}, False),
                ('both_stops', {'CANARY_PEARL_ACCEPTED_QUOTA': '2', 'CANARY_PEARL_SUSTAINED_MODE': 'private-v1',
                               'CANARY_PEARL_SUSTAINED_SECONDS': '1'}, False),
            ])
            for key, value in [('CANARY_PEARL_EXPERIMENT', 'sm120-wm8-wn2-tm2-tn4-fixed-k2048-r128-v1'),
                               ('CANARY_PEARL_TILE', 'wm4'), ('CANARY_PEARL_PROFILE_INTERVAL', '1'),
                               ('CANARY_PEARL_A_ROOT_FUSION', '1'), ('CANARY_PEARL_ATTEMPT_OVERLAP', '1'),
                               ('CANARY_PEARL_HARD_TARGET', '1'), ('CANARY_PEARL_BENCH_ALL_HIT', '1')]:
                cases.append(('conflict_' + key, {key: value}, False))
            with tempfile.TemporaryDirectory(prefix='pearl-shape-cli-') as directory:
                command = [str(binary), '--algo', 'pearl-pow', '--pool', '127.0.0.1:1',
                    '--wallet', 'prl1' + 'q' * 59, '--pearl-network', 'mainnet',
                    '--pearl-target-semantics', 'base-before-penalty', '--pearl-transcript',
                    str(Path(directory) / 'native'), '--batch', '256']
                for name, changes, ordinary in cases:
                    env = baseline.copy()
                    if not ordinary:
                        env.update(CANARY_PEARL_SHAPE_EXPERIMENT=PROFILE, CANARY_PEARL_SHAPE=SHAPE)
                    for key, value in changes.items():
                        if value is None:
                            env.pop(key, None)
                        else:
                            env[key] = value
                    result = subprocess.run(command, env=env, stdin=subprocess.DEVNULL,
                                            capture_output=True, timeout=5)
                    output = result.stdout + result.stderr
                    expected = 97 if ordinary else 1
                    if result.returncode != expected or (MARKER in output) != ordinary:
                        raise AssertionError((label, mode, name, result.returncode,
                                              result.stdout, result.stderr))
                    if name == 'exact_pair_no_owned_guard':
                        diagnostic = (b'shape experiment requires an owned bounded guard'
                                      if label == 'shape_capable' else b'invalid shape experiment')
                        if diagnostic not in output:
                            raise AssertionError((label, mode, name, 'wrong admission boundary', output))
                    checks += 1
    print('PASS real inert shape CLI admission checks:', checks)


if __name__ == '__main__':
    main()
