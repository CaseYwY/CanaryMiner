"""Private CPU-only stdin/stdout protocol fixtures, with no game/device/network or installed worker activation."""
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time

SCOPE = '1' * 32
WORKER = '2' * 32


class Probe:
    def __init__(self, exe):
        self.process = subprocess.Popen([str(exe)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.lines = queue.Queue(maxsize=128)

        def read():
            for line in iter(lambda: self.process.stdout.readline(385), b''):
                self.lines.put(line.decode('ascii').rstrip('\n'))

        self.reader = threading.Thread(target=read, daemon=True)
        self.reader.start()
        ready = self.line().split()
        assert len(ready) == 2 and ready[0] == 'CMREADY1'
        self.issued = int(ready[1])

    def line(self):
        return self.lines.get(timeout=5)

    def command(self, revision, directive='P', lease=2000):
        return f'CMWC1 {SCOPE} 1 0 1 {WORKER} {revision} {self.issued} {self.issued + lease} {directive}\n'.encode('ascii')

    def send(self, wire):
        self.process.stdin.write(wire)
        self.process.stdin.flush()

    def receipt(self, revision, kind):
        while True:
            fields = self.line().split()
            assert fields[0] == 'CMWA1' and len(fields) == 12
            if int(fields[6]) == revision and fields[10] == kind:
                self.issued = int(fields[11])
                return fields

    def finish(self, expected, close=True):
        if close:
            self.process.stdin.close()
        while True:
            line = self.line().split()
            if line[0] == 'CMDONE1':
                break
            assert line[0] == 'CMWA1'
        if not close:
            self.process.stdin.close()
        assert line[1] == expected
        assert self.process.wait(timeout=5) == (0 if expected == 'stopped' else 8)
        self.reader.join(timeout=5)
        assert not self.reader.is_alive()
        self.process.stdout.close()
        self.process.stderr.close()
        return int(line[2]), int(line[3])

    def cleanup(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait(timeout=5)
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            if stream and not stream.closed:
                stream.close()


def main():
    exe = Path(sys.argv[1]).resolve()
    checks = 0
    for mode in range(8):
        probe = Probe(exe)
        try:
            if mode == 6:
                probe.send(probe.command(1, 'R'))
                assert probe.finish('fault', close=False)[0] == 0
                checks += 1
                continue
            if mode == 7:
                wire = probe.command(1).replace(WORKER.encode(), b'3' * 32)
                probe.send(wire)
                assert probe.finish('fault', close=False)[0] == 0
                checks += 1
                continue
            wire = probe.command(1, lease=350 if mode in (1, 3) else 2000)
            # Split a real OS pipe frame inside its version and identity, not just a string-parser test.
            for fragment in (wire[:3], wire[3:21], wire[21:]):
                probe.send(fragment)
            paused = probe.receipt(1, 'P')
            assert int(paused[11]) < int(paused[8])
            checks += 1
            if mode in (0, 2):
                run = probe.command(2, 'R')
                probe.send(run)
                probe.receipt(2, 'A')
                time.sleep(0.05)
                probe.send(probe.command(3))
                probe.receipt(3, 'P')
                if mode == 2:
                    probe.send(run)  # old revision cannot undo a newer acknowledged pause
                    batches, drains = probe.finish('fault', close=False)
                else:
                    batches, drains = probe.finish('stopped')
                assert batches > 0 and drains >= 2
                checks += 1
            elif mode in (1, 3):
                if mode == 3:
                    probe.send(probe.command(2, 'R')[:-1])  # no LF: not a command and cannot renew the old lease
                assert probe.finish('fault', close=False)[0] == 0
                checks += 1
            elif mode == 4:
                probe.send(b'x' * 385)
                assert probe.finish('fault', close=False)[0] == 0
                checks += 1
            else:
                probe.send(probe.command(2, 'R')[:40])
                assert probe.finish('stopped')[0] == 0  # EOF is terminal stop even during a partial frame
                checks += 1
        finally:
            probe.cleanup()
    print(f'worker control private pipes: {checks} checks; fragmented frames, boundary receipts, expiry, replay, overflow and EOF')


if __name__ == '__main__':
    main()
