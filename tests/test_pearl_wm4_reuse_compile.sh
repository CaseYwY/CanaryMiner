#!/bin/sh
# CPU-only closed contract compilation. No CUDA bodies or generated sources.
set -eu
test "$#" -eq 1
compiler=$1
out=$(mktemp "${TMPDIR:-/tmp}/pearl-wm4-reuse-contract.XXXXXX")
trap 'rm -f "$out"' EXIT HUP INT TERM
cases=0
reject() {
    source=$1; message=$2; shift 2
    status=0
    "$compiler" -std=c++17 -Wall -Wextra -Werror -pedantic-errors -fsyntax-only "$@" "$source" >"$out" 2>&1 || status=$?
    test "$status" -eq 1
    grep -F "$message" "$out" >/dev/null
    cases=$((cases+1))
}
contract=tests/test_pearl_ptx_fixed_contract.cpp
pair=tests/test_pearl_wm4_reuse_pair.cpp
for value in 0 2 -1 malformed; do
    reject "$contract" 'CANARY_PEARL_ENABLE_WM4_REUSE_EXPERIMENT must be absent or exactly 1' "-DCANARY_PEARL_ENABLE_WM4_REUSE_EXPERIMENT=$value"
done
for flag in CANARY_PEARL_TEST_PTX_INLINE_BODY CANARY_PEARL_TEST_WM8_REGCAP CANARY_PEARL_ENABLE_WM8_CAP112_EXPERIMENT CANARY_PEARL_DIRECT_FOLD_MODE; do
    for value in 0 1; do
        reject "$contract" 'WM4 reuse backend requires original global body, launch qualifier and general fold' \
            -DCANARY_PEARL_ENABLE_WM4_REUSE_EXPERIMENT=1 "-D$flag=$value"
    done
done
for flag in CANARY_PEARL_TEST_RESOURCE_ONLY CANARY_PEARL_TEST_WM4_REUSE_RESOURCE CANARY_PEARL_TEST_WM8_FIXED_RESOURCE CANARY_PEARL_TEST_BK32_FIXED_RESOURCE; do
    for value in 0 1; do
        reject "$contract" 'WM4 reuse backend cannot combine standalone resource-probe admission' \
            -DCANARY_PEARL_ENABLE_WM4_REUSE_EXPERIMENT=1 "-D$flag=$value"
    done
done
for value in 0 2 -1; do
    reject "$pair" 'WM4 reuse search energy mode must be exactly 1' \
        "-DCANARY_PEARL_TEST_WM4_REUSE_SEARCH_ENERGY=$value" -DCANARY_PEARL_TEST_WM4_REUSE_BACKEND=1
    reject "$pair" 'WM4 reuse backend mode must be exactly 1' \
        -DCANARY_PEARL_TEST_WM4_REUSE_SEARCH_ENERGY=1 "-DCANARY_PEARL_TEST_WM4_REUSE_BACKEND=$value"
done
for flag in CANARY_PEARL_TEST_WM4_REUSE_SEARCH_ENERGY CANARY_PEARL_TEST_WM4_REUSE_BACKEND; do
    reject "$pair" 'WM4 reuse search energy requires explicit paired energy and backend modes' "-D$flag=1"
    reject "$pair" 'malformed' -DCANARY_PEARL_TEST_WM4_REUSE_SEARCH_ENERGY=1 -DCANARY_PEARL_TEST_WM4_REUSE_BACKEND=1 "-U$flag" "-D$flag=malformed"
done
reject "$pair" 'WM4 reuse and BK32 backend modes are mutually exclusive' \
    -DCANARY_PEARL_TEST_WM4_REUSE_SEARCH_ENERGY=1 -DCANARY_PEARL_TEST_WM4_REUSE_BACKEND=1 \
    -DCANARY_PEARL_TEST_BK32_SEARCH_ENERGY=1 -DCANARY_PEARL_TEST_BK32_BACKEND=1
test "$cases" -eq 31
echo 'WM4 reuse compile contract: PASS (31 expected CPU compiler refusals)'
