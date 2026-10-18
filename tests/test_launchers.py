"""The launchers are argument pass-through and nothing else."""
import os
import shutil
import stat
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAUNCHERS = os.path.join(ROOT, 'launchers')


def code_lines(path, comment_prefixes):
    with open(path, encoding='utf8') as f:
        return [l.rstrip('\r\n') for l in f if l.strip() and not l.strip().lower().startswith(comment_prefixes)]


class LauncherTests(unittest.TestCase):
    def test_sh_launcher_is_one_exec_line(self):
        lines = code_lines(os.path.join(LAUNCHERS, 'canaryminer.sh'), ('#',))
        self.assertEqual(lines, ['exec "$(dirname "$0")/canary-miner" "$@"'])

    def test_bat_launcher_is_one_call_and_an_exit_code(self):
        lines = code_lines(os.path.join(LAUNCHERS, 'canaryminer.bat'), ('rem',))
        self.assertEqual(lines, ['@echo off', '"%~dp0canary-miner.exe" %*', 'exit /b %ERRORLEVEL%'])

    def test_no_config_or_policy_words_in_launchers(self):
        for name in ('canaryminer.sh', 'canaryminer.bat'):
            with open(os.path.join(LAUNCHERS, name), encoding='utf8') as f:
                text = f.read().lower()
            code = '\n'.join(l for l in text.splitlines() if not l.strip().startswith(('#', 'rem')))
            for word in ('wallet', 'pool', 'worker', '--', 'set ', 'export', 'if ', 'for '):
                self.assertNotIn(word, code, (name, word))

    @unittest.skipIf(os.name == 'nt' or shutil.which('sh') is None, 'POSIX shell required')
    def test_sh_launcher_forwards_arguments_and_exit_status(self):
        with tempfile.TemporaryDirectory() as d:
            shutil.copy(os.path.join(LAUNCHERS, 'canaryminer.sh'), os.path.join(d, 'canaryminer.sh'))
            stub = os.path.join(d, 'canary-miner')
            with open(stub, 'w') as f:
                f.write('#!/bin/sh\nfor a in "$@"; do printf "[%s]" "$a"; done\nprintf "\\n"\nexit 7\n')
            os.chmod(stub, os.stat(stub).st_mode | stat.S_IXUSR)
            r = subprocess.run(['sh', os.path.join(d, 'canaryminer.sh'), 'run', '--pool', 'two words', '--worker', "it's", ''], capture_output=True, text=True)
            self.assertEqual(r.returncode, 7)
            self.assertEqual(r.stdout.strip(), "[run][--pool][two words][--worker][it's][]")


if __name__ == '__main__':
    unittest.main()
