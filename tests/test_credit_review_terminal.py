"""Real native settings panel plus isolated SQLite owner. Synthetic fixture only; no pool/miner/GPU."""
import json
import os
from pathlib import Path
import select
import struct
import subprocess
import sys
import tempfile
import time


def main():
    if os.name != 'posix':
        raise SystemExit('POSIX controlling-terminal test; Windows uses native panel/process tests')
    import fcntl
    import pty
    import termios
    fixture, helper, cli = [str(Path(p).resolve()) for p in sys.argv[1:4]]
    cursored = len(sys.argv) == 5 and sys.argv[4] == '--cursor'
    suffix = '-cursor' if cursored else ''
    expected_prepared = 'Prepared 0 0 0' + (' cursor=0' if cursored else '')
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-credit-review-') as directory:
        root = Path(directory)
        subprocess.run([fixture, '--prepare' + suffix, str(root)], check=True, capture_output=True, timeout=5)
        journal = root / 'fixture-credit-review.db.credit-recovery.json'
        before = journal.read_bytes()

        def facts():
            return subprocess.check_output([fixture, '--facts' + suffix, str(root)], timeout=5).decode().strip()

        assert facts() == expected_prepared
        rejected = subprocess.run([fixture, '--settings' + suffix, helper, str(root)], capture_output=True, timeout=5)
        assert rejected.returncode == 2 and journal.read_bytes() == before
        checks += 2

        def session(work, command=None):
            nonlocal checks
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 40, 100, 0, 0))

            def own_terminal():
                os.setsid()
                fcntl.ioctl(0, termios.TIOCSCTTY, 0)

            child = subprocess.Popen(command or [fixture, '--settings' + suffix, helper, str(root)], stdin=slave, stdout=slave, stderr=slave, preexec_fn=own_terminal)
            os.close(slave)
            transcript = b''

            def until(needle, fresh=False):
                nonlocal transcript
                if fresh:
                    transcript = b''
                deadline = time.monotonic() + 8
                while needle not in transcript:
                    if time.monotonic() > deadline:
                        raise AssertionError('credit review terminal readiness deadline')
                    if select.select([master], [], [], 0.05)[0]:
                        try:
                            data = os.read(master, 65536)
                        except OSError:
                            raise AssertionError('credit review terminal closed before expected state') from None
                        if not data:
                            raise AssertionError('credit review terminal EOF before expected state')
                        transcript += data
                return transcript

            def send(data):
                os.write(master, data)

            try:
                until(b'CanaryMiner / Settings')
                send(b'\t ' + b'\x1b[D' * 3 + b'\x1b[B' * 2 + b'\r')  # independent unsaved UI color draft, then Privacy field 3
                until(b'Credit recovery / local collection status', True)
                work(send, until)
                deadline = time.monotonic() + 5
                while child.poll() is None and time.monotonic() < deadline:
                    if select.select([master], [], [], 0.05)[0]:
                        try:
                            transcript += os.read(master, 65536)
                        except OSError:
                            break
                assert child.wait(timeout=1) == 0
                assert not (root / 'ui-preferences.json').exists()  # review/retry never saves a separate UI draft
                checks += 2
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait(timeout=5)
                os.close(master)

        def ordinary_unavailable(send, until):
            nonlocal checks
            until(b'no qualified local credit collector/owner is active')
            send(b'ri \rf')
            until(b'no store was opened', True)
            assert journal.read_bytes() == before and facts() == expected_prepared
            checks += 2
            send(b'q')

        config = root / 'synthetic-config.json'
        config.write_text(json.dumps({'schema_version': 1, 'state_dir': str(root)}), encoding='utf8')
        session(ordinary_unavailable, [cli, 'settings', '--config', str(config)])

        def inspect_only(send, until):
            nonlocal checks
            until(b'NeedsReconciliation')
            screen = until(b'PRIVATE values hidden')  # a PTY read can split the header/status/body across chunks
            assert b'PRIVATE values hidden' in screen and b'fixture.private-original' not in screen and b'2.5 PRL' not in screen
            if cursored:
                assert b'Committed source checkpoint: Ok | sequence 0' in screen and b'INIT' not in screen and b'TAIL' not in screen
                checks += 1
            assert journal.read_bytes() == before
            send(b'r\r')
            until(b'No request sent.', True)
            assert journal.read_bytes() == before
            send(b'i')
            if cursored:
                until(b'Initial cursor:', True)
                send(b'\x1b[6~' * 4)
                until(b'TAIL', True)
                send(b'\x1b[6~' * 2)
                checks += 1
            until(b'fixture.private-original', True)
            send(b' ')
            until(b'[x] EXACT replay', True)
            send(b'\x1b')
            until(b'Retry review cancelled', True)
            assert facts() == expected_prepared and journal.read_bytes() == before
            checks += 5
            send(b'q')

        session(inspect_only)

        def explicit_retry(send, until):
            nonlocal checks
            until(b'NeedsReconciliation')
            send(b'ri ')
            until(b'[x] EXACT replay', True)
            send(b'\r\r\r')  # one reviewed admission only, not three retries
            until(b'Recovery: Confirmed', True)
            until(b'confirmed: Ok with persisted receipt')  # an immediate helper can finish before a queued frame renders
            confirmed = facts()
            assert confirmed == 'Confirmed 1 2.5 1800000000000' + (' cursor=1' if cursored else '')
            assert journal.read_bytes() != before
            send(b'r')
            until(b'Explicit retry unavailable', True)
            assert facts() == confirmed
            send(b'f')
            until(b'Inspection QUEUED', True)
            until(b'Queue pending 0', True)  # do not race the independent facts probe against an in-flight helper lease
            assert facts() == confirmed
            checks += 5
            send(b'\x03')  # actual controlling-terminal SIGINT; owner stop occurs outside safety/render callbacks

        session(explicit_retry)
        assert not list(root.glob('*preferences.json')) and not list(root.glob('*worker-starts*'))
        checks += 1
    print(f'credit review{suffix} PTY: {checks} checks; ordinary unavailable/inspect/cancel/one reviewed retry/SIGINT/private defaults passed')


if __name__ == '__main__':
    main()
