#!/bin/sh
# Actual CLI linked only to inert SDK/factory stubs. The positive path exits97
# at the first device probe; no CUDA/Vulkan SDK, backend or network is entered.
# Constructor-sticky conflicts are tested by the pure preparation-plan suite;
# this separate process-level gate tests current CLI options, not backend reuse.
set -eu
test "$#" -eq 2
missing=${1:?missing-capability stub required}
capable=${2:?WM4-reuse-capable stub required}
out=$(mktemp "${TMPDIR:-/tmp}/pearl-wm4-reuse-cli.XXXXXX")
trap 'rm -f "$out"' EXIT HUP INT TERM
selector=sm120-wm4-reuse-wn2-tm4-tn4-fixed-k2048-r128-v1
live='--algo pearl-pow --pool 127.0.0.1:1 --wallet prl1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq --pearl-network mainnet --pearl-target-semantics base-before-penalty'
# the product CLI keeps every --pool (ordered list), so a different endpoint replaces the one in $live instead of being appended
live_at() { printf '%s' "$live" | sed "s|--pool 127.0.0.1:1|--pool $1|"; }
cases=0
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
        test "$expected" -eq 97
        grep -F 'FORBIDDEN_DEVICE_SETUP (host CLI stub)' "$out" >/dev/null
    fi
    cases=$((cases + 1))
}
# Fixed literal options intentionally word-split below; never user input.
check 1 "CANARY_PEARL_EXPERIMENT=$selector" "$missing" $live
check 97 "CANARY_PEARL_EXPERIMENT=$selector" "$capable" $live
for value in '' malformed 'sm120-wm4-reuse-wn2-tm4-tn4-fixed-k2048-r128-v1 ' \
    sm86-wm2-wn4-tm4-tn2-bk32-fixed-k2048-r128-v1 \
    retired-wm2-wn4-tm4-tn2-fixed-k2048-r128-v1 \
    sm120-wm2-wn4-tm4-tn2-bk32-fixed-k2048-r128-v1 \
    sm120-wm8-wn2-tm2-tn4-runtime-v1 sm120-fixed-k2048-r128-v1 \
    sm120-wm8-wn2-tm2-tn4-runtime-inline-cap112-v1 sm120-wm8-wn2-tm2-tn4-fixed-k2048-r128-v1; do
    check 1 "CANARY_PEARL_EXPERIMENT=$value" "$capable" $live
done
# Empty and zero are presence, never an off switch. The capable stub also
# advertises overlap/fusion support so negatives do not rely on missing SDKs.
for variable in CANARY_PEARL_PROFILE_INTERVAL CANARY_PEARL_SHAPE CANARY_PEARL_TILE CANARY_PEARL_ATTEMPT_OVERLAP CANARY_PEARL_A_ROOT_FUSION CANARY_PEARL_HARD_TARGET CANARY_PEARL_BENCH_ALL_HIT; do
    for value in '' 0 malformed; do
        check 1 "CANARY_PEARL_EXPERIMENT=$selector" "$variable=$value" "$capable" $live
    done
done
check 1 "CANARY_PEARL_EXPERIMENT=$selector" CANARY_PEARL_ATTEMPT_OVERLAP=a-prep-v1 "$capable" $live
check 1 "CANARY_PEARL_EXPERIMENT=$selector" CANARY_PEARL_ATTEMPT_OVERLAP=a-prep64-v1 "$capable" $live
check 1 "CANARY_PEARL_EXPERIMENT=$selector" CANARY_PEARL_A_ROOT_FUSION=a-root-commit-v1 "$capable" $live
for host in localhost:1 127.0.0.2:1 public.invalid:1; do
    check 1 "CANARY_PEARL_EXPERIMENT=$selector" "$capable" $(live_at "$host")
done
check 1 "CANARY_PEARL_EXPERIMENT=$selector" "$capable" --algo autolykos2 --pool 127.0.0.1:1 --wallet dummy
check 1 "CANARY_PEARL_EXPERIMENT=$selector" "$capable" --algo pearl-pow --bench
check 1 "CANARY_PEARL_EXPERIMENT=$selector" "$capable" --algo pearl-pow --bench --bench-formal \
    --bench-start-nonce 0x0123456789abcdef --pearl-shape-id m4096-n65536 \
    --pearl-kernel-id cuda-ptx-mma-s8-v1 --pearl-tile-id wm2-wn4-tm2-tn2-bk64-st3-gm8 \
    --plain --batch 4194304 --interval 5
# A clean subsequent process must still reach only the inert positive seam.
check 97 "CANARY_PEARL_EXPERIMENT=$selector" "$capable" $live
test "$cases" -eq 43
echo 'WM4 reuse CLI admission: PASS (43 cases, inert stubs only)'
