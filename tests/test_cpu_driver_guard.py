"""Check refusal before loading; never performs an unguarded driver lookup."""
import subprocess
import sys


def main():
    binary = sys.argv[1]
    result = subprocess.run([binary, 'self'], capture_output=True, timeout=10)
    assert result.returncode == 0 and not result.stderr
    for library in ('libcuda.so.1', '/owned/missing/libnvidia-ml.so.1', 'NvMl.dll',
                    'libOpenCL.so', 'libamdhip64.so', 'libhsa-runtime64.so', 'librocm.so'):
        result = subprocess.run([binary, library], capture_output=True, timeout=10)
        assert result.returncode == 95, (library, result.returncode)
        assert result.stderr == b'CPU fixture refused driver library\n'
        assert not result.stdout
    print('CPU driver guard: self-loader and seven pre-load refusals passed')


if __name__ == '__main__':
    if not __debug__:
        raise RuntimeError('assertions require non-optimized Python')
    main()
