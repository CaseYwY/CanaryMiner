# Contributing to CanaryMiner

Thank you for looking at CanaryMiner. The project is MIT-licensed; contributions are accepted under the same license.

## Before you start

- Open an issue to discuss a change that alters mining behaviour, the pool protocol, the developer donation, or the kernels.
- Keep changes small and focused. One logical change per commit.

## Sign-off (DCO)

Every commit must carry a `Signed-off-by:` line certifying the [Developer Certificate of Origin](https://developercertificate.org/).
Use `git commit -s`. Commits without it are not merged.

## Gates

A change is ready to merge only when these pass:

1. **Host tests and secret scan.** `tools/test_all.sh` on Linux or WSL with the pinned CUDA 13.1 toolkit and the
   pinned Pearl verifier. The script needs the local, untracked `.secrets.local` deny-list for the secret scan, and it
   never skips a stage.
2. **Kernel SASS equality.** Per-kernel SASS of the native build must equal
   `tools/gate1a/final-sass.json` (67 kernels for SM120). `tools/check_sass_invariant.py` enforces this and
   runs inside `tools/test_all.sh`. A change that alters kernel SASS is a kernel change and needs review of its
   mathematics, not only of its build.
3. **Protocol identity.** The serialized proof, `mining.submit` and `mining.authorize` bytes must match the pinned values,
   and the miner's proofs must be accepted by the pinned verifier.

## Rules

- **No hand-written SASS.** Kernel changes go through the compiler (CUDA C++ or PTX). Do not hand-assemble or patch SASS.
- **Never change kernel math** as a side effect of a refactor. Mathematical changes need their own justification and
  evidence.
- **No secrets in the tree.** Do not commit payout addresses, wallet material, pool credentials, rig names, host names,
  or local paths. `tools/check_secrets.py` enforces this and runs in `tools/test_all.sh`.
- **Report results honestly.** Include the exact commands you ran and their output summaries. If a stage fails or was not
  run, say so.
