#!/usr/bin/env bash
# Build the Linux x86-64 release archive for one published tag.
#
# Input:  a source tarball of the tag (git archive <tag>, no top-level folder), the pinned CUDA 13.1 toolkit root,
#         the version, the tag's commit time (unix seconds), the NVIDIA driver minimum for CUDA 13.1 (text for QUICKSTART),
#         and an output folder.
# Output: <out>/canaryminer-<version>-linux-x86_64.tar.gz and <out>/SHA256SUMS.
# Deterministic: sorted entries, fixed mtime, owner/group 0, normalized modes, gzip -n.
set -euo pipefail

usage() {
  echo "usage: $0 --source <source.tar> --toolkit <cuda-13.1-root> --version <X.Y.Z> --epoch <unix-seconds> --min-driver <text> --out <dir> --deny-file <file> [--work <dir>]" >&2
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
[ -n "$source_tar" ] && [ -n "$toolkit" ] && [ -n "$version" ] && [ -n "$epoch" ] && [ -n "$min_driver" ] && [ -n "$out" ] && [ -f "$deny_file" ] || usage
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "make_linux_release: version must be X.Y.Z" >&2; exit 2; }
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
[ ! -e "$work" ] && [ ! -L "$work" ] || { echo "make_linux_release: work directory already exists; preserve it and choose a new directory" >&2; exit 1; }
mkdir -p "$work"

mkdir -p "$work/src"
tar -xf "$source_tar" -C "$work/src"

(
  cd "$work/src"
  cmake --preset production-sm120 -DCMAKE_CUDA_COMPILER="$nvcc" -DCANARY_ENABLE_ENERGY_RUNTIME=OFF -DCANARY_ENABLE_WORKER_CONTROL_RUNTIME=OFF >"$work/configure.log" 2>&1
  cmake --build --preset production-sm120 --parallel "$(nproc)" >"$work/build.log" 2>&1
)
native="$work/src/build/production-sm120/canary-miner"
[ -x "$native" ] || { echo "make_linux_release: build did not produce canary-miner" >&2; exit 1; }
# Ordinary preferences require their fixed CPU sibling; unrelated development runtimes remain unqualified.
grep -qx 'CANARY_ENABLE_ENERGY_RUNTIME:BOOL=OFF' "$work/src/build/production-sm120/CMakeCache.txt" || {
  echo "make_linux_release: development energy runtime is not release-qualified" >&2; exit 1;
}
grep -qx 'CANARY_ENABLE_WORKER_CONTROL_RUNTIME:BOOL=OFF' "$work/src/build/production-sm120/CMakeCache.txt" || {
  echo "make_linux_release: development worker control is not release-qualified" >&2; exit 1;
}

name="canaryminer-$version"
stage="$work/stage/$name"
helper="$(dirname "$native")/canary_preferences_worker"
[ -x "$helper" ] && [ ! -L "$helper" ] || { echo "make_linux_release: mandatory preference helper unavailable" >&2; exit 1; }
mkdir -p "$stage"
cp "$native" "$stage/canary-miner"
cp "$helper" "$stage/canary_preferences_worker"
strip --strip-all "$stage/canary-miner"
strip --strip-all "$stage/canary_preferences_worker"

# Metadata-only inspection covers BOTH images, every enabled development capability, and the private deny-list.
# The inspected images are never executed. The reported floor includes the native glibc 2.40 capability contract.
glibc="$(python3 -B "$work/src/packaging/release/verify_artifacts.py" --platform linux --stage "$stage" --source "$work/src" \
  --cache "$work/src/build/production-sm120/CMakeCache.txt" --deny-file "$deny_file" --tool "$(command -v objdump)")"
# The standard layout: miner + mandatory CPU preference helper, editable start files, plain-text readme, licences.
for f in start.sh start-f2pool.sh start-kryptex.sh start-herominers.sh start-advanced.sh; do
  cp "$work/src/launchers/$f" "$stage/$f"
done
cp "$work/src/config/config.example.json" "$stage/config.example.json"
cp "$work/src/LICENSE" "$work/src/THIRD_PARTY.md" "$work/src/HELP.txt" "$stage/"
cp -r "$work/src/LICENSES" "$stage/LICENSES"

sed -e "s|@VERSION@|$version|g" -e "s|@MIN_DRIVER@|$min_driver|g" -e "s|@GLIBC@|$glibc|g" \
  "$work/src/packaging/release/README-linux.txt" >"$stage/README.txt"

# Normalize modes so the archive does not depend on the build host's umask.
find "$stage" -type d -exec chmod 755 {} +
find "$stage" -type f -exec chmod 644 {} +
chmod 755 "$stage/canary-miner" "$stage/canary_preferences_worker" "$stage"/start*.sh

# The archive holds exactly the standard layout: a file added or dropped by mistake fails the release here.
want="$(printf '%s\n' HELP.txt LICENSE LICENSES README.txt THIRD_PARTY.md canary-miner canary_preferences_worker config.example.json \
  start-advanced.sh start-f2pool.sh start-herominers.sh start-kryptex.sh start.sh)"
got="$(cd "$stage" && LC_ALL=C ls -A1)"
if [ "$got" != "$want" ]; then
  echo "make_linux_release: the archive does not match the standard layout:" >&2
  diff <(printf '%s\n' "$want") <(printf '%s\n' "$got") >&2 || true
  exit 1
fi

mkdir -p "$out"
archive="$out/$name-linux-x86_64.tar.gz"
[ ! -e "$archive" ] && [ ! -L "$archive" ] && [ ! -e "$out/SHA256SUMS" ] && [ ! -L "$out/SHA256SUMS" ] || { echo "make_linux_release: output already exists; preserve it" >&2; exit 1; }
set -o noclobber
tar --sort=name --mtime="@$epoch" --owner=0 --group=0 --numeric-owner --format=gnu \
  -C "$work/stage" -cf - "$name" | gzip -n -9 >"$archive"

(cd "$out" && sha256sum "$(basename "$archive")" >SHA256SUMS)

echo "archive: $archive"
echo "sha256: $(sha256sum "$archive" | cut -d' ' -f1)"
echo "glibc-max: $glibc"
