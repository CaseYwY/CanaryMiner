#!/bin/sh
# Real CLI, inert factories only; successful admission stops at sentinel 97.
set -eu
missing=${1:?missing-capability stub required}
capable=${2:?fixed-WM8-capable stub required}
out=$(mktemp "${TMPDIR:-/tmp}/pearl-wm8-fixed-cli.XXXXXX")
trap 'rm -f "$out"' EXIT HUP INT TERM
selector=sm120-wm8-wn2-tm2-tn4-fixed-k2048-r128-v1
live='--algo pearl-pow --pool 127.0.0.1:1 --wallet prl1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq --pearl-network mainnet --pearl-target-semantics base-before-penalty'
# the product CLI keeps every --pool (ordered list), so a different endpoint replaces the one in $live instead of being appended
live_at() { printf '%s' "$live" | sed "s|--pool 127.0.0.1:1|--pool $1|"; }
check() {
    expected=$1; shift
    status=0
    env -u CANARY_PEARL_EXPERIMENT -u CANARY_PEARL_ATTEMPT_OVERLAP -u CANARY_PEARL_A_ROOT_FUSION \
        -u CANARY_PEARL_PROFILE_INTERVAL -u CANARY_PEARL_SHAPE -u CANARY_PEARL_TILE \
        -u CANARY_PEARL_HARD_TARGET -u CANARY_PEARL_BENCH_ALL_HIT "$@" >"$out" 2>&1 || status=$?
    if [ "$status" -ne "$expected" ]; then cat "$out" >&2; exit 1; fi
    if [ "$expected" -eq 1 ]; then
        grep -F 'refusing before device/source setup' "$out" >/dev/null
        if grep -F FORBIDDEN_DEVICE_SETUP "$out" >/dev/null; then exit 1; fi
    else
        grep -F FORBIDDEN_DEVICE_SETUP "$out" >/dev/null
    fi
}
# Fixed fixture arguments below intentionally word-split; never user input.
check 1 "CANARY_PEARL_EXPERIMENT=$selector" "$missing" $live
check 97 "CANARY_PEARL_EXPERIMENT=$selector" "$capable" $live
for value in '' malformed sm120-wm8-wn2-tm2-tn4-runtime-v1 sm120-fixed-k2048-r128-v1 sm120-wm8-wn2-tm2-tn4-runtime-inline-cap112-v1; do
    check 1 "CANARY_PEARL_EXPERIMENT=$value" "$capable" $live
done
for variable in CANARY_PEARL_PROFILE_INTERVAL CANARY_PEARL_SHAPE CANARY_PEARL_TILE CANARY_PEARL_ATTEMPT_OVERLAP CANARY_PEARL_A_ROOT_FUSION CANARY_PEARL_HARD_TARGET CANARY_PEARL_BENCH_ALL_HIT; do
    for value in '' 0 malformed; do
        check 1 "CANARY_PEARL_EXPERIMENT=$selector" "$variable=$value" "$capable" $live
    done
done
check 1 "CANARY_PEARL_EXPERIMENT=$selector" CANARY_PEARL_ATTEMPT_OVERLAP=a-prep64-v1 "$capable" $live
check 1 "CANARY_PEARL_EXPERIMENT=$selector" CANARY_PEARL_A_ROOT_FUSION=a-root-commit-v1 "$capable" $live
for host in localhost:1 127.0.0.2:1 public.invalid:1; do
    check 1 "CANARY_PEARL_EXPERIMENT=$selector" "$capable" $(live_at "$host")
done
check 1 "CANARY_PEARL_EXPERIMENT=$selector" "$capable" --algo pearl-pow --bench
check 1 "CANARY_PEARL_EXPERIMENT=$selector" "$capable" --algo pearl-pow --bench --bench-formal \
    --bench-start-nonce 0x0123456789abcdef --pearl-shape-id m4096-n65536 \
    --pearl-kernel-id cuda-ptx-mma-s8-v1 --pearl-tile-id wm2-wn4-tm2-tn2-bk64-st3-gm8 \
    --plain --batch 4194304 --interval 5
echo 'fixed WM8 CLI admission: PASS (inert stubs only)'
