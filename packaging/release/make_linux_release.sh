#!/usr/bin/env bash
# Build the Linux x86-64 release archive for one published tag.
#
# Input:  a source tarball of the tag (git archive <tag>, no top-level folder), the pinned CUDA 13.1 toolkit root,
#         the version, the tag's commit time (unix seconds), the NVIDIA driver minimum for CUDA 13.1 (text for QUICKSTART),
#         and an output folder.
# Output: <out>/canaryminer-<version>-linux-x86_64.tar.gz and <out>/SHA256SUMS.
# Deterministic: sorted entries, fixed mtime, owner/group 0, normalized modes, gzip -n.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"

usage() {
  echo "usage: $0 --source <source.tar> --toolkit <cuda-13.1-root> --version <X.Y.Z> --epoch <unix-seconds> --min-driver <text> --out <dir> [--work <dir>] [--deny-file <file>]" >&2
  exit 2
}

source_tar=""; toolkit=""; version=""; epoch=""; min_driver=""; out=""; work=""; deny_file=""
while [ $# -gt 0 ]; do
  case "$1" in
    --source) source_tar="${2:-}"; shift 2 ;;
    --toolkit) toolkit="${2:-}"; shift 2 ;;
    --version) version="${2:-}"; shift 2 ;;
    --epoch) epoch="${2:-}"; shift 2 ;;
    --min-driver) min_driver="${2:-}"; shift 2 ;;
    --out) out="${2:-}"; shift 2 ;;
    --work) work="${2:-}"; shift 2 ;;
    --deny-file) deny_file="${2:-}"; shift 2 ;;
    *) usage ;;
  esac
done
[ -n "$source_tar" ] && [ -n "$toolkit" ] && [ -n "$version" ] && [ -n "$epoch" ] && [ -n "$min_driver" ] && [ -n "$out" ] || usage
case "$epoch" in ''|*[!0-9]*) echo "make_linux_release: --epoch must be unix seconds" >&2; exit 2 ;; esac
[ -f "$source_tar" ] || { echo "make_linux_release: no source tarball: $source_tar" >&2; exit 2; }

# The pinned toolkit is a build-time input only; it is never copied into the archive.
nvcc="$toolkit/bin/nvcc"
"$nvcc" --version | grep -E 'release 13\.1, V13\.1\.115' >/dev/null \
  || { echo "make_linux_release: $nvcc is not CUDA 13.1.115" >&2; exit 1; }
cmake_version="$(cmake --version | head -1 | sed -n 's/^cmake version \([0-9.]*\).*/\1/p')"
case "$cmake_version" in
  3.3[1-9]*|3.[4-9]*|[4-9].*) ;;
  *) echo "make_linux_release: cmake $cmake_version is older than 3.31" >&2; exit 1 ;;
esac

# Build in a fixed, neutral folder (default /build/canaryminer-<version>) so no home or user path is compiled into the binary.
# The toolkit should also sit at a neutral path (for example /opt/cuda-13.1): nvcc embeds its include directories.
[ -n "$work" ] || work="/build/canaryminer-$version"
case "$toolkit$work" in */home/*|*/Users/*|*/mnt/*) echo "make_linux_release: toolkit and work dir must not be under /home, /Users or /mnt" >&2; exit 1 ;; esac
rm -rf "$work"; mkdir -p "$work"
trap 'rm -rf "$work"' EXIT

mkdir -p "$work/src"
tar -xf "$source_tar" -C "$work/src"

(
  cd "$work/src"
  cmake --preset production-sm120 -DCMAKE_CUDA_COMPILER="$nvcc" >"$work/configure.log" 2>&1
  cmake --build --preset production-sm120 --parallel "$(nproc)" >"$work/build.log" 2>&1
)
native="$work/src/build/production-sm120/canary-miner"
[ -x "$native" ] || { echo "make_linux_release: build did not produce canary-miner" >&2; exit 1; }

name="canaryminer-$version"
stage="$work/stage/$name"
mkdir -p "$stage"
cp "$native" "$stage/canary-miner"
strip --strip-all "$stage/canary-miner"

# Refuse to ship a binary that names a build machine. Two checks on the stripped binary's printable strings:
#  1. home/user paths and WSL mounts (always);
#  2. the owner's local deny-list, through tools/check_secrets.py, so the exact public tokens (the maintainer handle and the
#     donation account) stay allowed and everything else on the list fails (only when --deny-file is given).
leaks="$(grep -aoE '/home/[^/[:space:]]+|/Users/[^/[:space:]]+|[A-Za-z]:[\/]Users[\/][^\/[:space:]]+|/mnt/[a-z]/' "$stage/canary-miner" | sort -u || true)"
if [ -n "$leaks" ]; then
  echo "make_linux_release: the binary embeds build-machine paths; not packaging:" >&2
  printf '%s\n' "$leaks" >&2
  exit 1
fi
if [ -n "$deny_file" ]; then
  CANARY_SECRETS_FILE="$deny_file" python3 - "$work/src/tools" "$stage/canary-miner" <<'PY' || exit 1
import re, sys
sys.path.insert(0, sys.argv[1])
import check_secrets as c
_, local = c.local_patterns('.')
data = open(sys.argv[2], 'rb').read()
text = '\n'.join(m.group().decode() for m in re.finditer(rb'[\x20-\x7e]{6,}', data))
# The compiled-in donation address is public by design (README, src/product/fee.h); drop exactly that string.
fee = re.search(r'#define CANARY_FEE_DESTINATION "(prl1[a-z0-9]+)"', open(sys.argv[1] + '/../src/product/fee.h').read()).group(1)
# The compiler may store it split or merged with a neighbouring string, so mask every run that starts with its first 30 chars.
text = re.sub(re.escape(fee[:30]) + r'[a-z0-9]*', '<donation-address>', text)
findings = []
c.scan_text('canary-miner', text.encode(), local, findings)
bad = sorted({f[2] for f in findings})
if bad:
    print('make_linux_release: the binary matches the deny-list: ' + ', '.join(bad), file=sys.stderr)
    sys.exit(1)
PY
fi
# The standard layout (H-031 amendment): one binary, ready-to-edit start files, plain-text readme, licences.
for f in start.sh start-f2pool.sh start-kryptex.sh start-herominers.sh start-advanced.sh; do
  cp "$work/src/launchers/$f" "$stage/$f"
done
cp "$work/src/config/config.example.json" "$stage/config.example.json"
cp "$work/src/LICENSE" "$work/src/THIRD_PARTY.md" "$work/src/HELP.txt" "$stage/"
cp -r "$work/src/LICENSES" "$stage/LICENSES"

# Runtime dependencies: only the driver's libcuda.so.1 and the system C/C++ runtime may be needed.
deps="$(ldd "$stage/canary-miner")"
if printf '%s\n' "$deps" | grep -E 'libcudart|libcublas|libcurand|libcufft|libnvrtc|libnvJitLink|libnvvm|libcudnn' >/dev/null; then
  echo "make_linux_release: canary-miner links CUDA toolkit libraries:" >&2
  printf '%s\n' "$deps" >&2
  exit 1
fi
glibc="$(objdump -T "$stage/canary-miner" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1 | sed 's/GLIBC_//')"

sed -e "s|@VERSION@|$version|g" -e "s|@MIN_DRIVER@|$min_driver|g" -e "s|@GLIBC@|$glibc|g" \
  "$here/README-linux.txt" >"$stage/README.txt"

# Normalize modes so the archive does not depend on the build host's umask.
find "$stage" -type d -exec chmod 755 {} +
find "$stage" -type f -exec chmod 644 {} +
chmod 755 "$stage/canary-miner" "$stage"/start*.sh

# The archive holds exactly the standard layout: a file added or dropped by mistake fails the release here.
want="$(printf '%s\n' HELP.txt LICENSE LICENSES README.txt THIRD_PARTY.md canary-miner config.example.json \
  start-advanced.sh start-f2pool.sh start-herominers.sh start-kryptex.sh start.sh)"
got="$(cd "$stage" && LC_ALL=C ls -A1)"
if [ "$got" != "$want" ]; then
  echo "make_linux_release: the archive does not match the standard layout:" >&2
  diff <(printf '%s\n' "$want") <(printf '%s\n' "$got") >&2 || true
  exit 1
fi

mkdir -p "$out"
archive="$out/$name-linux-x86_64.tar.gz"
tar --sort=name --mtime="@$epoch" --owner=0 --group=0 --numeric-owner --format=gnu \
  -C "$work/stage" -cf - "$name" | gzip -n -9 >"$archive"

(cd "$out" && sha256sum "$(basename "$archive")" >SHA256SUMS)

echo "archive: $archive"
echo "sha256: $(sha256sum "$archive" | cut -d' ' -f1)"
echo "glibc-max: $glibc"
