# Review a legacy first-pool migration to TLS

Open **Pools / TLS declarations** in the dashboard settings or run `canary-miner settings --config FILE`. Press **M** to review a supported legacy first pool.

| Configured TCP first pool | Proposed strict TLS endpoint |
| --- | --- |
| `kryptex` or `prl-us.kryptex.network:7048` | `stratum+ssl://prl-us.kryptex.network:8048` |
| `prl-hk.kryptex.network:7048` | `stratum+ssl://prl-hk.kryptex.network:8048` |

The proposal requires a clean draft and an `Unchanged` first-user declaration with empty URLs and no plaintext consent. Existing declarations can be edited through the ordinary fields. [Official Kryptex endpoint table](https://pool.kryptex.com/articles/pearl-miners-en)

The review shows the configured TCP source, proposed TLS endpoint, disabled plaintext fallback, and any startup constraints. Use Up/Down or PgUp/PgDn to read the review on a short terminal. **Enter** saves through the existing preference owner. **Esc** returns to the draft, which can be discarded through the ordinary leave prompt.

Save writes the first user declaration to `pool-security-preferences.json`. The main configuration remains the source for the wallet, protocol parameters and configured first pool. The dashboard still shows the running session's frozen policy and observed connection. The next explicit start reads the saved Strict TLS declaration and applies the ordinary first-pool admission rules, including a matching DNS host, a TLS-enabled build and donation off.

The proposal uses the configured first-pool tuple captured after command-line overrides. Each proposal shares its target with the corresponding strict Kryptex preset and keeps the configured region. [Strict TLS presets](pool-tls-presets.md)

If the independent preferences file changes after it was loaded, the save can become UNKNOWN. The original review cannot be replayed by restoring the old file bytes. Explicit inspection can establish a new editable baseline, after which **M** and **Enter** are separate actions again.

## Hong Kong migration verification

Linux passed 11 focused groups, including 30 PTY checks each for Hong Kong and US migration. Windows passed seven focused groups. The TLS-enabled production settings command passed three Hong Kong scenarios on each platform. Windows production SASS matched all 67 frozen digests.

The unchanged `tools/test_all.sh` passed at `55eaf0529d900e84a2f9f068fd9a6073923f4e43`. CTest recorded 627 passes, zero failures and one namespace-related skip. The skipped game lifecycle script passed 35 checks separately with the same artifacts. Python discovery recorded 176 passes and 19 skips. The pinned proof verifier, protocol comparisons, secret scan and 67 Linux SASS comparisons passed. The local receipt is at `build/p12n-gate-evidence/receipt.json`.

## US migration verification

The Linux TLS-enabled host build passed seven focused groups after rerunning the affected message tests. The initial message test incorrectly expected a 41-cell atomic URL to wrap at 19 cells. The corrected test verifies refusal at 19 and 40 cells and full display at 41. Independent review also found that the PTY test could accept a buffered wide frame. The 30-check rerun verifies the bounds of each accepted 20×12 review frame, both complete endpoints, the review end marker and explicit Save. The previous nine-check pool-security PTY regression also passed.

The actual Linux production `settings` command passed three isolated PTY cases covering review and save, a donation warning with exit, and an unmatched port. Windows passed seven focused groups after rerunning the message test and the groups not reached in the first run. The TLS-enabled Windows production build passed all 67 SASS comparisons. The actual Windows command passed three ConPTY cases covering save, discard and the donation warning. All three cases exited successfully and reaped their child processes. The first harness run rejected the expected empty preference lock file after a successful Save. The corrected harness accepts that known lock and still checks the exact saved declaration and all other files.

P-12 still requires further preset coverage, donation TLS, real pool/GPU sessions and release qualification. Full Windows host-suite qualification remains subject to the previously recorded Defender block.

The unchanged `tools/test_all.sh` passed at `ea38f6726639acabf81c960c7c85e7003574fa34` in the pinned Linux toolchain. CTest ran 627 entries in 539.95 seconds with 626 passes, zero failures and one namespace-related skip. The original game lifecycle script passed 35 checks separately using the same artifacts. Python discovery recorded 176 passes and 19 skips. The pinned verifier accepted the proof, protocol comparisons matched, the history secret scan was clean and all 67 SASS digests matched both frozen golden sets. The local receipt is at `build/p12m-gate-evidence/receipt.json`.

The first two gate runs are retained as failures. The energy lifecycle test could receive ESRCH when a helper exited during a procfs read. The bootstrap terminal test could read the startup marker before its first complete line. Error injection reproduced each failure, and both fixed tests passed their isolated reruns and the full gate. The fixes are confined to the two test scripts.
