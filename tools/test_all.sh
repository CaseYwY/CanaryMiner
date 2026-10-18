#!/bin/sh
# Single CPU-only entry point for the host-side checks (no GPU, no network, no miner binary is run).
#
#   tools/test_all.sh                 # configure + build + run everything in the CMake 'tests' preset
#   CMAKE=/path/to/cmake CTEST=/path/to/ctest tools/test_all.sh
#   CANARY_VERIFIER_PYTHONPATH=/dir/with/pearl_mining CANARY_TOOLKIT=/cuda-13.1-root tools/test_all.sh
#
# Three stages are MANDATORY (they fail, never skip, when their input is missing):
#   - the pinned Pearl verifier check inside protocol_identity (CANARY_VERIFIER_PYTHONPATH),
#   - the secret / private-path scan (needs the untracked .secrets.local deny-list, or CANARY_SECRETS_FILE),
#   - the kernel SASS invariant (CANARY_TOOLKIT: CUDA 13.1 root with nvcc and cuobjdump).
#
# What runs (ctest names in parentheses):
#   - 38 C++ host test programs built with -Wall -Wformat -Werror=format (CLI host stubs with -Werror)
#   - the protocol-identity gate (protocol_identity): serialized PlainProof, mining.submit and mining.authorize bytes
#     equal the pinned values from the pre-debrand seed and the miner-encoded proof is accepted by the pinned verifier
#   - the Python unit tests (python_unittests) and the mock-pool contract tests
#   - the secret / private-path scan (check_secrets), including a history scan below
#   - a negative build-hygiene test: a bare '%' in a printf format must not compile (format_error_class_is_an_error)
#   - the kernel SASS invariant (tools/check_sass_invariant.py): the Blackwell native is built with the recipe and every
#     kernel's SASS must equal tools/gate1a/final-sass.json
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
CMAKE=${CMAKE:-cmake}
CTEST=${CTEST:-ctest}
: "${CANARY_VERIFIER_PYTHONPATH:?test_all: CANARY_VERIFIER_PYTHONPATH must point at the pinned Pearl verifier (mandatory)}"
: "${CANARY_TOOLKIT:?test_all: CANARY_TOOLKIT must point at a CUDA 13.1 toolkit root (SASS invariant stage is mandatory)}"
if [ ! -f .secrets.local ] && [ -z "${CANARY_SECRETS_FILE:-}" ]; then
    echo "test_all: the untracked .secrets.local deny-list is missing (mandatory secret scan)" >&2
    exit 2
fi
"$CMAKE" --preset tests
"$CMAKE" --build --preset tests --parallel
"$CTEST" --preset tests
if [ -d .git ]; then python3 tools/check_secrets.py --history; fi
python3 tools/check_sass_invariant.py
echo "test_all: OK"
