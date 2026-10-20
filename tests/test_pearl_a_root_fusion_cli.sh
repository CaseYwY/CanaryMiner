#!/bin/sh
# Actual CLI admission linked only to inert host stubs. Device setup exits 97.
set -eu
missing=${1:?missing-capability host stub required}
capable=${2:?A-root-fusion-capable host stub required}
out=$(mktemp "${TMPDIR:-/tmp}/pearl-a-root-fusion-cli.XXXXXX")
trap 'rm -f "$out"' EXIT HUP INT TERM
wm8=sm120-wm8-wn2-tm2-tn4-runtime-v1
live='--algo pearl-pow --pool 127.0.0.1:1 --wallet prl1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq --pearl-network mainnet --pearl-target-semantics base-before-penalty'
# the product CLI keeps every --pool (ordered list), so a different endpoint replaces the one in $live instead of being appended
live_at() { printf '%s' "$live" | sed "s|--pool 127.0.0.1:1|--pool $1|"; }
clean() {
    env -u CANARY_PEARL_A_ROOT_FUSION -u CANARY_PEARL_ATTEMPT_OVERLAP \
        -u CANARY_PEARL_EXPERIMENT -u CANARY_PEARL_PROFILE_INTERVAL \
        -u CANARY_PEARL_SHAPE -u CANARY_PEARL_TILE -u CANARY_PEARL_HARD_TARGET \
        -u CANARY_PEARL_BENCH_ALL_HIT "$@"
}
check() {
    label=$1 expected=$2 diagnostic=$3
    shift 3
    status=0
    clean "$@" >"$out" 2>&1 || status=$?
    if [ "$status" -ne "$expected" ]; then
        echo "$label: expected $expected, got $status" >&2
        cat "$out" >&2
        exit 1
    fi
    grep -F -- "$diagnostic" "$out" >/dev/null
    if [ "$expected" -eq 1 ] && grep -F FORBIDDEN_DEVICE_SETUP "$out" >/dev/null; then
        echo "$label reached forbidden device setup" >&2
        exit 1
    fi
}
refusal='invalid or non-isolated A root fusion'
# Only fixed test fixture strings are intentionally word-split below.
check missing 1 "$refusal" CANARY_PEARL_A_ROOT_FUSION=a-root-commit-v1 \
    "CANARY_PEARL_EXPERIMENT=$wm8" "$missing" $live
check admitted 97 FORBIDDEN_DEVICE_SETUP CANARY_PEARL_A_ROOT_FUSION=a-root-commit-v1 \
    "CANARY_PEARL_EXPERIMENT=$wm8" "$capable" $live
for selector in '' malformed a-root-commit-v2; do
    check root-selector 1 "$refusal" "CANARY_PEARL_A_ROOT_FUSION=$selector" \
        "CANARY_PEARL_EXPERIMENT=$wm8" "$capable" $live
done
for selector in '' malformed sm120-fixed-k2048-r128-v1 sm120-wm8-wn2-tm2-tn4-runtime-inline-cap112-v1; do
    check wrong-gemm 1 "$refusal" CANARY_PEARL_A_ROOT_FUSION=a-root-commit-v1 \
        "CANARY_PEARL_EXPERIMENT=$selector" "$capable" $live
done
check missing-gemm 1 "$refusal" CANARY_PEARL_A_ROOT_FUSION=a-root-commit-v1 "$capable" $live
for profile in '' 0 1 malformed; do
    check profile-present 1 "$refusal" CANARY_PEARL_A_ROOT_FUSION=a-root-commit-v1 \
        "CANARY_PEARL_EXPERIMENT=$wm8" "CANARY_PEARL_PROFILE_INTERVAL=$profile" "$capable" $live
done
for endpoint in localhost:1 127.0.0.2:1 example.invalid:1; do
    check wrong-host 1 "$refusal" CANARY_PEARL_A_ROOT_FUSION=a-root-commit-v1 \
        "CANARY_PEARL_EXPERIMENT=$wm8" "$capable" $(live_at "$endpoint")
done
for endpoint in 127.0.0.1:0 127.0.0.1:65536; do
    check invalid-port 3 'neither a preset (kryptex, herominers, f2pool), HOST:PORT, nor stratum+tcp://HOST:PORT' CANARY_PEARL_A_ROOT_FUSION=a-root-commit-v1 \
        "CANARY_PEARL_EXPERIMENT=$wm8" "$capable" $(live_at "$endpoint")
done
check wrong-algorithm 1 "$refusal" CANARY_PEARL_A_ROOT_FUSION=a-root-commit-v1 \
    "CANARY_PEARL_EXPERIMENT=$wm8" "$capable" --algo autolykos2 --pool 127.0.0.1:1 --wallet dummy
check synthetic 1 "$refusal" CANARY_PEARL_A_ROOT_FUSION=a-root-commit-v1 \
    "CANARY_PEARL_EXPERIMENT=$wm8" "$capable" --algo pearl-pow --bench
check formal 1 "$refusal" CANARY_PEARL_A_ROOT_FUSION=a-root-commit-v1 \
    "CANARY_PEARL_EXPERIMENT=$wm8" "$capable" --algo pearl-pow --bench --bench-formal \
    --bench-start-nonce 0x0123456789abcdef --pearl-shape-id m4096-n65536 \
    --pearl-kernel-id cuda-ptx-mma-s8-v1 --pearl-tile-id wm2-wn4-tm2-tn2-bk64-st3-gm8 \
    --plain --batch 4194304 --interval 5
echo 'A-root fusion CLI admission: ok (inert host stubs only)'
