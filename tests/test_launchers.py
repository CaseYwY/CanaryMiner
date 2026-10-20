"""The launchers: the pass-through pair stays argument pass-through; the start files edit one address and never echo it."""
import glob
import os
import re
import shutil
import stat
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAUNCHERS = os.path.join(ROOT, 'launchers')
TEST_WALLET = 'prl1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq'  # placeholder-shaped, never a real address
PLACEHOLDER = 'PUT_YOUR_PRL_ADDRESS_HERE'


def code_lines(path, comment_prefixes):
    with open(path, encoding='utf8') as f:
        return [l.rstrip('\r\n') for l in f if l.strip() and not l.strip().lower().startswith(comment_prefixes)]


def read(name):
    with open(os.path.join(LAUNCHERS, name), encoding='utf8') as f:
        return f.read()


class PassThroughTests(unittest.TestCase):
    def test_sh_launcher_is_one_exec_line(self):
        lines = code_lines(os.path.join(LAUNCHERS, 'canaryminer.sh'), ('#',))
        self.assertEqual(lines, ['exec "$(dirname "$0")/canary-miner" "$@"'])

    def test_bat_launcher_is_one_call_and_an_exit_code(self):
        lines = code_lines(os.path.join(LAUNCHERS, 'canaryminer.bat'), ('rem',))
        self.assertEqual(lines, ['@echo off', '"%~dp0canary-miner.exe" %*', 'exit /b %ERRORLEVEL%'])

    def test_no_config_or_policy_words_in_pass_through(self):
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


class StartFileTests(unittest.TestCase):
    """The standard layout: each start file has one address to edit and never prints it."""

    SH_START = sorted(os.path.basename(p) for p in glob.glob(os.path.join(LAUNCHERS, 'start*.sh')))
    BAT_START = sorted(os.path.basename(p) for p in glob.glob(os.path.join(LAUNCHERS, 'start*.bat')))

    def test_the_standard_set_is_present(self):
        self.assertEqual(self.SH_START, ['start-advanced.sh', 'start-f2pool.sh', 'start-herominers.sh', 'start-kryptex.sh', 'start.sh'])
        self.assertEqual(self.BAT_START, ['start-advanced.bat', 'start-f2pool.bat', 'start-herominers.bat', 'start-kryptex.bat', 'start.bat'])

    def test_every_start_file_has_the_one_address_to_edit(self):
        for name in self.SH_START + self.BAT_START:
            with self.subTest(name=name):
                self.assertTrue('YOUR_PRL_ADDRESS' in read(name) or PLACEHOLDER in read(name))

    def test_batch_files_use_crlf_line_endings(self):
        for name in self.BAT_START + ['canaryminer.bat']:
            with open(os.path.join(LAUNCHERS, name), 'rb') as f:
                data = f.read()
            with self.subTest(name=name):
                self.assertNotIn(b'\n', data.replace(b'\r\n', b''))

    def test_no_start_file_echoes_the_wallet(self):
        for name in self.SH_START + self.BAT_START:
            for line in read(name).splitlines():
                if re.match(r'\s*(echo|printf)\b', line, re.I):
                    with self.subTest(name=name, line=line):
                        self.assertNotIn('$WALLET', line)
                        self.assertNotIn('%WALLET%', line)

    def test_batch_files_call_the_miner_by_full_path(self):
        # Windows may disable running programs from the current folder by bare name (NoDefaultCurrentDirectoryInExePath),
        # so every start file calls "%~dp0canary-miner.exe", never a bare canary-miner.exe.
        for name in self.BAT_START:
            for line in read(name).splitlines():
                if re.match(r'\s*"?(%~dp0)?canary-miner\.exe', line, re.I):
                    with self.subTest(name=name, line=line):
                        self.assertTrue(line.lstrip().startswith('"%~dp0canary-miner.exe"'))

    def test_each_batch_start_file_keeps_its_window_open(self):
        for name in self.BAT_START:
            if name == 'start-advanced.bat':
                continue  # it loops until Ctrl+C; the pause happens on errors
            with self.subTest(name=name):
                self.assertIn('\r\npause\r\n', read(name).replace('\n', '\r\n'))

    def test_advanced_bat_refuses_the_placeholder_and_restarts(self):
        text = read('start-advanced.bat')
        self.assertIn('if "%WALLET%"=="' + PLACEHOLDER + '" (', text)
        self.assertIn('goto run', text)
        self.assertIn('--no-donation', text)

    def test_advanced_sh_refuses_the_placeholder_and_restarts(self):
        text = read('start-advanced.sh')
        self.assertIn('if [ "$WALLET" = "%s" ]' % PLACEHOLDER, text)
        self.assertIn('RESTART', text)
        self.assertIn('while :; do', text)

    @unittest.skipIf(os.name == 'nt' or shutil.which('sh') is None, 'POSIX shell required')
    def test_start_sh_runs_the_miner_with_the_standard_flags(self):
        with tempfile.TemporaryDirectory() as d:
            shutil.copy(os.path.join(LAUNCHERS, 'start.sh'), os.path.join(d, 'start.sh'))
            stub = os.path.join(d, 'canary-miner')
            with open(stub, 'w') as f:
                f.write('#!/bin/sh\nfor a in "$@"; do printf "[%s]" "$a"; done\nprintf "\\n"\nexit 4\n')
            os.chmod(stub, os.stat(stub).st_mode | stat.S_IXUSR)
            r = subprocess.run(['sh', os.path.join(d, 'start.sh'), '--gpu-id', '0'], capture_output=True, text=True)
            self.assertEqual(r.returncode, 4)
            self.assertEqual(r.stdout.strip(),
                             '[--pool][stratum+tcp://pearl.f2pool.com:5500][--wallet][YOUR_PRL_ADDRESS.rig1][--gpu-id][0]')

    @unittest.skipIf(os.name == 'nt' or shutil.which('sh') is None, 'POSIX shell required')
    def test_advanced_sh_refuses_the_placeholder_before_starting_the_miner(self):
        with tempfile.TemporaryDirectory() as d:
            shutil.copy(os.path.join(LAUNCHERS, 'start-advanced.sh'), os.path.join(d, 'start-advanced.sh'))
            marker = os.path.join(d, 'started')
            stub = os.path.join(d, 'canary-miner')
            with open(stub, 'w') as f:
                f.write('#!/bin/sh\ntouch "%s"\n' % marker)
            os.chmod(stub, os.stat(stub).st_mode | stat.S_IXUSR)
            r = subprocess.run(['sh', os.path.join(d, 'start-advanced.sh')], capture_output=True, text=True)
            self.assertEqual(r.returncode, 1)
            self.assertIn('Edit WALLET', r.stderr)
            self.assertFalse(os.path.exists(marker))

    @unittest.skipIf(os.name == 'nt' or shutil.which('sh') is None, 'POSIX shell required')
    def test_advanced_sh_summary_hides_the_wallet_and_maps_donation_off(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(LAUNCHERS, 'start-advanced.sh'), encoding='utf8') as f:
                text = f.read()
            text = text.replace('WALLET="PUT_YOUR_PRL_ADDRESS_HERE"', 'WALLET="%s"' % TEST_WALLET)
            text = text.replace('DONATION="on"', 'DONATION="off"').replace('RESTART=1', 'RESTART=0')
            script = os.path.join(d, 'start-advanced.sh')
            with open(script, 'w', encoding='utf8') as f:
                f.write(text)
            os.chmod(script, os.stat(script).st_mode | stat.S_IXUSR)
            stub = os.path.join(d, 'canary-miner')
            with open(stub, 'w') as f:
                f.write('#!/bin/sh\nfor a in "$@"; do printf "[%s]" "$a"; done\nprintf "\\n"\nexit 0\n')
            os.chmod(stub, os.stat(stub).st_mode | stat.S_IXUSR)
            r = subprocess.run(['sh', script], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0)
            first, second = r.stdout.splitlines()[:2]
            self.assertEqual(first, 'CanaryMiner: pool f2pool, worker rig1, donation off.')
            self.assertIn('--no-donation', second)
            self.assertNotIn(TEST_WALLET, first)


if __name__ == '__main__':
    unittest.main()
