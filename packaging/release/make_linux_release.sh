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
  echo "usage: $0 --source <source.tar> --toolkit <cuda-13.1-root> --version <X.Y.Z> --epoch <unix-seconds> --min-driver <text> --out <dir>" >&2
  exit 2
}

source_tar=""; toolkit=""; version=""; epoch=""; min_driver=""; out=""
while [ $# -gt 0 ]; do
  case "$1" in
    --source) source_tar="${2:-}"; shift 2 ;;
    --toolkit) toolkit="${2:-}"; shift 2 ;;
    --version) version="${2:-}"; shift 2 ;;
    --epoch) epoch="${2:-}"; shift 2 ;;
    --min-driver) min_driver="${2:-}"; shift 2 ;;
    --out) out="${2:-}"; shift 2 ;;
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

work="$(mktemp -d)"
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
cp "$work/src/launchers/canaryminer.sh" "$stage/canaryminer.sh"
cp "$work/src/config/config.example.json" "$stage/config.example.json"
cp "$work/src/README.md" "$work/src/LICENSE" "$work/src/TRADEMARK.md" "$work/src/THIRD_PARTY.md" "$stage/"
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
  "$here/QUICKSTART.txt" >"$stage/QUICKSTART.txt"

# Normalize modes so the archive does not depend on the build host's umask.
find "$stage" -type d -exec chmod 755 {} +
find "$stage" -type f -exec chmod 644 {} +
chmod 755 "$stage/canary-miner" "$stage/canaryminer.sh"

mkdir -p "$out"
archive="$out/$name-linux-x86_64.tar.gz"
tar --sort=name --mtime="@$epoch" --owner=0 --group=0 --numeric-owner --format=gnu \
  -C "$work/stage" -cf - "$name" | gzip -n -9 >"$archive"

(cd "$out" && sha256sum "$(basename "$archive")" >SHA256SUMS)

echo "archive: $archive"
echo "sha256: $(sha256sum "$archive" | cut -d' ' -f1)"
echo "glibc-max: $glibc"
