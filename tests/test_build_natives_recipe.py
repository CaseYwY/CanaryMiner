"""Build command construction only; a fake compiler cannot qualify CUDA or kernel SASS."""
import contextlib
import io
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import threading
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]


class NativeBuildRecipeTests(unittest.TestCase):
    def recipe(self, only):
        calls = []
        lock = threading.Lock()

        def compile_fixture(argv, **_):
            with lock:
                calls.append(argv)
            Path(argv[argv.index('-o') + 1]).write_bytes(b'synthetic compile fixture, not an executable')
            return SimpleNamespace(returncode=0, stdout='', stderr='')

        with tempfile.TemporaryDirectory(prefix='canary-recipe-') as directory:
            argv = ['build_natives.py', '120', '--toolkit', str(Path(directory) / 'fixture-toolkit'),
                    '--out', str(Path(directory) / 'fixture-output'), '--jobs', '2']
            if only:
                argv.append('--only')
            with mock.patch.object(sys, 'argv', argv), mock.patch.object(subprocess, 'run', side_effect=compile_fixture), contextlib.redirect_stdout(io.StringIO()):
                runpy.run_path(str(ROOT / 'tools' / 'build_natives.py'), run_name='__main__')
        return calls

    def test_full_link_contains_host_projector_and_original_kernel_recipe(self):
        calls = self.recipe(False)
        cuda = [argv for argv in calls if '-c' in argv and '-x' in argv]
        host = [argv for argv in calls if '-c' in argv and '-x' not in argv]
        link, = [argv for argv in calls if '-c' not in argv]
        self.assertEqual(len(cuda), 4)
        self.assertEqual({Path(argv[argv.index('-o') - 1]).name for argv in host}, {'exact_decimal.cpp', 'manual_market.cpp'})
        for argv in cuda:
            self.assertIn('-arch=sm_120a', argv)
            self.assertIn('--frandom-seed=2090ed724510a4ddf18dc3cd6bb5a58c7c9f6c5f967583fb499618d5146e58bf', argv)
        for argv in host:
            self.assertEqual(argv[0], '/usr/bin/g++')
            self.assertIn('-std=c++17', argv)
        self.assertEqual({Path(value).stem for value in link if value.endswith('.o')},
                         {'miner', 'registry', 'pearl_algo', 'run', 'exact_decimal', 'manual_market'})
        self.assertIn('-Xcompiler=-pthread', link)

    def test_kernel_only_gate_stays_compile_only(self):
        command, = self.recipe(True)
        self.assertIn('-c', command)
        self.assertEqual(Path(command[command.index('-o') - 1]).name, 'algo.cu')
        self.assertIn('-arch=sm_120a', command)


if __name__ == '__main__':
    unittest.main()
