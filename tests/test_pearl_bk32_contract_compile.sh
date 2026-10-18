#!/bin/sh
# CPU compiler only; exercise the same contract header included by CUDA.
set -eu
test "$#" -eq 1
compiler=$1
out=$(mktemp "${TMPDIR:-/tmp}/pearl-bk32-contract.XXXXXX")
trap 'rm -f "$out"' EXIT HUP INT TERM
cases=0
reject() {
    message=$1; shift
    status=0
    "$compiler" -std=c++17 -Wall -Wextra -Werror -pedantic-errors -fsyntax-only \
        "$@" tests/test_pearl_ptx_fixed_contract.cpp >"$out" 2>&1 || status=$?
    # An unavailable/killed compiler is not a successful negative.
    test "$status" -eq 1
    grep -F "$message" "$out" >/dev/null
    cases=$((cases + 1))
}
for value in 0 2 -1; do
    reject 'CANARY_PEARL_ENABLE_BK32_FIXED_EXPERIMENT must be absent or exactly 1' \
        "-DCANARY_PEARL_ENABLE_BK32_FIXED_EXPERIMENT=$value"
done
for flag in CANARY_PEARL_TEST_PTX_INLINE_BODY CANARY_PEARL_TEST_WM8_REGCAP \
    CANARY_PEARL_ENABLE_WM8_CAP112_EXPERIMENT CANARY_PEARL_DIRECT_FOLD_MODE; do
    for value in 0 1; do
        reject 'BK32 fixed backend requires original global body, launch qualifier and general fold' \
            -DCANARY_PEARL_ENABLE_BK32_FIXED_EXPERIMENT=1 "-D$flag=$value"
    done
done
test "$cases" -eq 11
echo 'BK32 compile contract: PASS (11 expected compiler refusals)'
