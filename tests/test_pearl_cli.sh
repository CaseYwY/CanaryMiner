#!/bin/sh
# CLI contract checks that finish before CUDA initialization or networking.

set -eu

bin=${1:?usage: test_pearl_cli.sh <canaryminer binary>}
out=$(mktemp "${TMPDIR:-/tmp}/canary-pearl-cli.XXXXXX")
trap 'rm -f "$out"' EXIT HUP INT TERM

"$bin" --help-all >"$out"
grep -F -- '--pool HOST:PORT  Pearl pool endpoint; requires --wallet and' "$out" >/dev/null
grep -F -- '--pearl-network mainnet' "$out" >/dev/null
grep -F -- '--pearl-target-semantics base-before-penalty|final-adjusted-bound' "$out" >/dev/null
grep -F -- '--pearl-transcript FILE  test-only Pearl pool metadata log' "$out" >/dev/null
grep -F -- '--bench-formal    reproducible synthetic Pearl CUDA benchmark' "$out" >/dev/null
grep -F -- '--bench-start-nonce 0xNNNNNNNNNNNNNNNN' "$out" >/dev/null

reject_retired() {
    if "$bin" --algo pearl-pow "$@" >"$out" 2>&1; then
        echo "accepted retired option: $*" >&2
        return 1
    fi
    grep -F -- 'unknown option' "$out" >/dev/null
}

reject_retired --gateway 127.0.0.1:8337
reject_retired --node 127.0.0.1
reject_retired --port 9053
reject_retired --api-key x
reject_retired --nvrtc
reject_retired --cache-dag on

if "$bin" --algo autolykos2 --pool example.invalid:1 --pearl-transcript "$out" >"$out" 2>&1; then
    echo 'accepted Pearl transcript outside Pearl pool mode' >&2
    exit 1
fi
grep -F -- '--pearl-transcript requires --algo pearl-pow with --pool.' "$out" >/dev/null

reject_formal() {
    expected=$1
    shift
    if "$bin" "$@" >"$out" 2>&1; then
        echo "accepted invalid formal benchmark command: $*" >&2
        return 1
    fi
    grep -F -- "$expected" "$out" >/dev/null
    if grep -F -- 'FORBIDDEN_DEVICE_SETUP' "$out" >/dev/null; then
        echo 'invalid Pearl command reached device setup' >&2
        return 1
    fi
}

reject_formal 'requires an explicit --pool HOST:PORT' --algo pearl-pow
reject_formal 'requires explicit --pearl-network and --pearl-target-semantics' --algo pearl-pow --pool offline.invalid:1 --wallet dummy
reject_formal 'requires explicit --pearl-network and --pearl-target-semantics' --algo pearl-pow --pool offline.invalid:1 --wallet dummy --pearl-network mainnet
for network in testnet testnet2 regtest; do
    reject_formal 'live runtime supports mainnet only' --algo pearl-pow --pool offline.invalid:1 --wallet dummy --pearl-network "$network" --pearl-target-semantics base-before-penalty
done
reject_formal 'requires one mainnet|testnet|testnet2|regtest value' --algo pearl-pow --pearl-network synthetic-benchmark
reject_formal 'requires one mainnet|testnet|testnet2|regtest value' --algo pearl-pow --pearl-network mainnet --pearl-network mainnet
reject_formal 'requires one base-before-penalty|final-adjusted-bound value' --algo pearl-pow --pearl-target-semantics nope
reject_formal 'rejects live source/policy options' --algo pearl-pow --bench --pool offline.invalid:1
reject_formal 'rejects live source/policy options' --algo pearl-pow --bench --pearl-network mainnet
reject_formal 'rejects live source/policy options' --algo pearl-pow --bench --worker canary
reject_formal 'requires its fixed fixture height' --algo pearl-pow --bench --bench-epoch-secs 1
reject_formal 'Pearl options require --algo pearl-pow' --algo autolykos2 --pearl-network mainnet
reject_formal 'invalid Pearl wallet' --algo pearl-pow --pool offline.invalid:1 --wallet invalid --pearl-network mainnet --pearl-target-semantics base-before-penalty

formal_required='--bench-formal requires --bench-start-nonce, --pearl-shape-id, --pearl-kernel-id, and --pearl-tile-id'
reject_formal "$formal_required" --bench --algo pearl-pow --bench-formal \
    --pearl-shape-id m4096-n65536 --pearl-kernel-id cuda-ptx-mma-s8-v1 \
    --pearl-tile-id wm2-wn4-tm2-tn2-bk64-st3-gm8
reject_formal "$formal_required" --bench --algo pearl-pow --bench-formal \
    --bench-start-nonce 0x0123456789abcdef \
    --pearl-kernel-id cuda-ptx-mma-s8-v1 \
    --pearl-tile-id wm2-wn4-tm2-tn2-bk64-st3-gm8
reject_formal "$formal_required" --bench --algo pearl-pow --bench-formal \
    --bench-start-nonce 0x0123456789abcdef --pearl-shape-id m4096-n65536 \
    --pearl-tile-id wm2-wn4-tm2-tn2-bk64-st3-gm8
reject_formal "$formal_required" --bench --algo pearl-pow --bench-formal \
    --bench-start-nonce 0x0123456789abcdef --pearl-shape-id m4096-n65536 \
    --pearl-kernel-id cuda-ptx-mma-s8-v1

reject_formal 'requires 0x followed by exactly 16 hexadecimal digits' \
    --bench --algo pearl-pow --bench-formal \
    --bench-start-nonce 0x0123456789abcdefjunk
reject_formal 'duplicate --bench-start-nonce option' \
    --bench --algo pearl-pow --bench-formal \
    --bench-start-nonce 0x0123456789abcdef \
    --bench-start-nonce 0x0123456789abcdef
reject_formal 'formal benchmark identity options require --bench-formal' \
    --bench --algo pearl-pow --bench-start-nonce 0x0123456789abcdef
reject_formal '--bench-formal requires CUDA --bench --algo pearl-pow' \
    --bench --algo autolykos2 --bench-formal
reject_formal 'rejects any explicit --bench-epoch-secs' \
    --bench --algo pearl-pow --bench-formal \
    --bench-start-nonce 0x0123456789abcdef --pearl-shape-id m4096-n65536 \
    --pearl-kernel-id cuda-ptx-mma-s8-v1 \
    --pearl-tile-id wm2-wn4-tm2-tn2-bk64-st3-gm8 \
    --plain --batch 4194304 --interval 5 --bench-epoch-secs 0

formal_protocol='--bench --algo pearl-pow --bench-formal --bench-start-nonce 0x0123456789abcdef --pearl-shape-id m4096-n65536 --pearl-kernel-id cuda-ptx-mma-s8-v1 --pearl-tile-id wm2-wn4-tm2-tn2-bk64-st3-gm8'
reject_formal 'requires exactly one each of --plain, --batch 4194304, and --interval 5' \
    $formal_protocol --batch 4194304 --interval 5
reject_formal 'requires exactly one each of --plain, --batch 4194304, and --interval 5' \
    $formal_protocol --plain --interval 5
reject_formal 'requires exactly one each of --plain, --batch 4194304, and --interval 5' \
    $formal_protocol --plain --batch 4194304
reject_formal 'requires exactly one each of --plain, --batch 4194304, and --interval 5' \
    $formal_protocol --plain --plain --batch 4194304 --interval 5
reject_formal 'requires exactly one each of --plain, --batch 4194304, and --interval 5' \
    $formal_protocol --plain --batch 4194304 --batch 4194304 --interval 5
reject_formal 'requires canonical --batch 4194304 and --interval 5 decimal tokens' \
    $formal_protocol --plain --batch 4194304junk --interval 5
reject_formal 'requires canonical --batch 4194304 and --interval 5 decimal tokens' \
    $formal_protocol --plain --batch 4194304 --interval 5junk
reject_formal 'requires canonical --batch 4194304 and --interval 5 decimal tokens' \
    $formal_protocol --plain --batch 04194304 --interval 5
reject_formal 'accepts only the exact decimal fixture height 1851444' \
    $formal_protocol --plain --batch 4194304 --interval 5 \
    --bench-height 1851444junk

for forbidden in '--mclk-offset 0' '--mem-oc'; do
    # Deliberately split fixed command fixtures.
    reject_formal 'rejects --mclk-offset and --mem-oc' \
        $formal_protocol --plain --batch 4194304 --interval 5 $forbidden
done

formal_args="$formal_protocol --plain --batch 4194304 --interval 5"
for variable in CANARY_PEARL_SHAPE CANARY_PEARL_TILE CANARY_PEARL_BENCH_ALL_HIT CANARY_PEARL_HARD_TARGET CANARY_PEARL_PROFILE_INTERVAL; do
    # Deliberately split formal_args: every token is a fixed test fixture.
    if env -u CANARY_PEARL_SHAPE -u CANARY_PEARL_TILE \
        -u CANARY_PEARL_BENCH_ALL_HIT -u CANARY_PEARL_HARD_TARGET -u CANARY_PEARL_PROFILE_INTERVAL \
        "$variable=1" "$bin" $formal_args >"$out" 2>&1; then
        echo "accepted formal benchmark with inherited $variable" >&2
        exit 1
    fi
    grep -F -- "rejects inherited environment variable $variable" "$out" >/dev/null
done

for value in '' 0 100 1000000000 malformed; do
    if env -u CANARY_PEARL_SHAPE -u CANARY_PEARL_TILE \
        -u CANARY_PEARL_BENCH_ALL_HIT -u CANARY_PEARL_HARD_TARGET \
        "CANARY_PEARL_PROFILE_INTERVAL=$value" "$bin" $formal_args >"$out" 2>&1; then
        echo 'accepted formal benchmark with profiling environment' >&2
        exit 1
    fi
    grep -F -- 'rejects inherited environment variable CANARY_PEARL_PROFILE_INTERVAL' "$out" >/dev/null
done

for selector in '' 1 malformed sm120-fixed-k2048-r128-v1 sm120-wm8-wn2-tm2-tn4-runtime-v1; do
    if env "CANARY_PEARL_EXPERIMENT=$selector" "$bin" --algo pearl-pow \
        --pool offline.invalid:1 --wallet invalid --pearl-network mainnet \
        --pearl-target-semantics base-before-penalty >"$out" 2>&1; then
        echo 'accepted non-isolated or malformed experimental selection' >&2
        exit 1
    fi
    grep -F -- 'invalid or non-isolated Pearl experiment; refusing before device/source setup' "$out" >/dev/null
done

reject_overlap() {
    selector=$1
    shift
    if env -u CANARY_PEARL_EXPERIMENT "CANARY_PEARL_ATTEMPT_OVERLAP=$selector" \
        "$bin" "$@" >"$out" 2>&1; then
        echo 'accepted invalid overlap selection' >&2
        exit 1
    fi
    grep -F -- 'invalid or non-isolated Pearl overlap; refusing before device/source setup' "$out" >/dev/null
    if grep -F -- 'FORBIDDEN_DEVICE_SETUP' "$out" >/dev/null; then
        echo 'invalid overlap reached device setup' >&2
        exit 1
    fi
}
for selector in '' 1 malformed a-prep-v1 a-prep64-v1; do
    reject_overlap "$selector" --algo pearl-pow --pool offline.invalid:1 \
        --wallet invalid --pearl-network mainnet --pearl-target-semantics base-before-penalty
done
reject_overlap a-prep-v1 --algo autolykos2 --pool 127.0.0.1:1
reject_overlap a-prep-v1 --algo pearl-pow --bench --pool 127.0.0.1:1
reject_overlap a-prep-v1 $formal_args
CANARY_PEARL_PROFILE_INTERVAL=1 reject_overlap a-prep-v1 --algo pearl-pow \
    --pool 127.0.0.1:1 --wallet invalid --pearl-network mainnet \
    --pearl-target-semantics base-before-penalty

echo 'pearl CLI contract: ok'
