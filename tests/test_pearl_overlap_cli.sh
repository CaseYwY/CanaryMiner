#!/bin/sh
# Both binaries are host-only stubs; a device/factory call exits 97, never CUDA.
set -eu
missing=${1:?missing-capability host stub required}
capable=${2:?capability-true host stub required}
out=$(mktemp "${TMPDIR:-/tmp}/pearl-overlap-cli.XXXXXX")
trap 'rm -f "$out"' EXIT HUP INT TERM
args='--algo pearl-pow --pool 127.0.0.1:1 --wallet prl1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq --pearl-network mainnet --pearl-target-semantics base-before-penalty'
for selector in a-prep-v1 a-prep64-v1; do
for mode in missing capable; do
    if [ "$mode" = missing ]; then bin=$missing; expected=1; else bin=$capable; expected=97; fi
    status=0
    env -u CANARY_PEARL_EXPERIMENT -u CANARY_PEARL_PROFILE_INTERVAL \
        "CANARY_PEARL_ATTEMPT_OVERLAP=$selector" "$bin" $args >"$out" 2>&1 || status=$?
    if [ "$status" -ne "$expected" ]; then
        echo "wrong overlap admission result for $mode: $status" >&2
        exit 1
    fi
    if [ "$mode" = missing ]; then
        grep -F 'invalid or non-isolated Pearl overlap' "$out" >/dev/null
        if grep -F FORBIDDEN_DEVICE_SETUP "$out" >/dev/null; then exit 1; fi
    else
        grep -F FORBIDDEN_DEVICE_SETUP "$out" >/dev/null
        if grep -F 'invalid or non-isolated Pearl overlap' "$out" >/dev/null; then exit 1; fi
    fi
done
done
echo 'overlap linked-capability CLI admission: ok (inert host stubs only)'
