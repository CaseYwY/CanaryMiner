"""Real process exits at prepared/applied/confirmed boundaries, with only synthetic offline facts."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    probe = str(Path(sys.argv[1]).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix='canary-journal-crash-') as directory:
        for phase, code in (('prepared', 75), ('applied', 76), ('confirmed', 77)):
            file = Path(directory) / (phase + '.db')
            result = subprocess.run([probe, phase, str(file)], capture_output=True, timeout=10)
            assert result.returncode == code and not result.stdout and not result.stderr
            sidecar = Path(str(file) + '.credit-recovery.json')
            before = sidecar.read_bytes()
            data = json.loads(subprocess.check_output([probe, 'inspect', str(file)], timeout=10))
            assert data[0] == 'CMCA2' and data[3] == 0 and sidecar.read_bytes() == before
            assert data[6][0] == 0 and data[6][1] == (3 if phase == 'confirmed' else 2)
            assert data[6][2][0] == 1 and data[6][2][3] == 1800000000000
            assert data[5][0][9] == ('0' if phase == 'prepared' else '2.5')
            recovered = json.loads(subprocess.check_output([probe, 'retry', str(file)], timeout=10))
            assert recovered[4] == (0 if phase == 'prepared' else 1)
            assert recovered[6][1] == 3 and recovered[5][0][9] == '2.5'
            assert recovered[5][0][5] == 1800000000000
            again = json.loads(subprocess.check_output([probe, 'retry', str(file)], timeout=10))
            assert again[4] == 1 and again[5][0][9] == '2.5'
            checks += 9
    print(f'credit journal crash: {checks} checks; prepared/apply/receipt process-death recovery passed')


if __name__ == '__main__':
    main()
