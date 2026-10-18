#!/bin/sh
# Host preprocessor contract only; no CUDA/NVML includes or device execution.
set -eu
test "$#" -eq 1
compiler=$1
out=$(mktemp "${TMPDIR:-/tmp}/pearl-bk32-energy-contract.XXXXXX")
trap 'rm -f "$out"' EXIT HUP INT TERM
check() {
    expected=$1; message=$2; shift 2
    status=0
    "$compiler" -std=c++17 -Wall -Wextra -Werror -pedantic-errors -fsyntax-only \
        "$@" tests/test_pearl_search_energy_pair.cpp >"$out" 2>&1 || status=$?
    test "$status" -eq "$expected"
    if test "$expected" -ne 0; then grep -F "$message" "$out" >/dev/null; fi
}
check 0 '' -DCANARY_PEARL_TEST_BK32_SEARCH_ENERGY=1 -DCANARY_PEARL_TEST_BK32_BACKEND=1
for value in 0 2 -1; do
    check 1 'BK32 search energy mode must be absent or exactly 1' \
        "-DCANARY_PEARL_TEST_BK32_SEARCH_ENERGY=$value" -DCANARY_PEARL_TEST_BK32_BACKEND=1
    check 1 'BK32 search energy backend mode must be absent or exactly 1' \
        -DCANARY_PEARL_TEST_BK32_SEARCH_ENERGY=1 "-DCANARY_PEARL_TEST_BK32_BACKEND=$value"
done
check 1 'BK32 search energy requires the explicit paired energy and backend modes' \
    -DCANARY_PEARL_TEST_BK32_SEARCH_ENERGY=1
check 1 'BK32 search energy requires the explicit paired energy and backend modes' \
    -DCANARY_PEARL_TEST_BK32_BACKEND=1
echo 'BK32 energy entry contract: PASS (one opt-in syntax pass, eight expected refusals)'
