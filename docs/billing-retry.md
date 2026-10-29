# Isolated reviewed monetary recovery (P-05m)

M-TARIFF/M-ENERGY; R-TAR-002–005, R-ENG-001, R-SAFE-001/002, R-PRIV-002 and R-REL-001 remain partial; T-TARIFF/T-ENERGY/T-INTENT/T-PRIVACY; D-52. This composes [complete readonly inspection](billing-query.md) with explicit isolated recovery of an existing [durable monetary original](billing-pending.md).

## Review before writable access

`BillingRecoveryStore::retryReviewed` is a single-use, existing-only operation. Under the journal lease it checks the already approved canonical-binding digest, exact journal revision and complete-original digest before opening the ledger writable. Only Prepared or Confirmed originals qualify. It then audits the ledger and invokes the existing exact retry; divergent state, stale review, mismatched input, missing/corrupt data or invalid clocks cannot become new write permission.

The helper never accepts new monetary chunks, creates a store, refreshes a receipt or queries the live energy source. The entire source query/reply remains in the private journal and is reloaded byte-exactly. A prepared original that was not committed applies once; one already committed returns Replay and confirms the original; a confirmed exact replay does not rewrite the journal. Failed retry retains Prepared under the earlier preservation rules.

## Bounded request and confirmation proof

`CMBRQ1` carries parent identity, generation/ticket, explicit ledger path, the three reviewed digests and current operation UTC instant. It is bounded to 32 KiB regardless of the full binding/receipt size. `CMBRA1` is bounded to 4 KiB and returns a status plus, only for successful exact recovery, the confirmed journal revision, its recorded result, committed chain digest and complete canonical monetary state. Existing public IPC limits are unchanged.

The parent retains the full inspected original and approved binding in memory. It validates its review revision before launch. On success it parses the bounded state, proves the original sequence/identity and chained monetary head, and reconstructs the complete confirmed journal checksum using the unchanged original preparation time, prior head, binding and chunk. An apparent success with a missing/wrong proof is an unknown transport outcome, never an acknowledged monetary total. Failure replies carry no committed-state certificate. Later readonly inspection resolves uncertainty; no automatic retry or re-fetch follows.

Generation/ticket bind the reply to the issued operation. A single client admits one transaction through final proof validation, with cancellation and one 50–60,000-ms deadline. Stop/lost-response handling still requires the later runtime owner to revoke publication; this helper does not start that owner. Windows owned jobs and Linux checked-parent/PDEATHSIG binding use the existing process transport. Unreaped outcomes remain explicit, and the helper path is supplied directly.

## Verification

`billing_retry` checks exact confirmed replay, strict request/reply framing, changed binding/revision/original, stale review after a later commit, locks, clock refusal, wrong helper/parent, malformed confirmation proof, single-flight Busy, cancellation and timeout. `billing_retry_crash` reconstructs Prepared input in independent owned processes, removes the synthetic energy source, then applies/replays through the actual helper. Pre-COMMIT, post-COMMIT and lost-acknowledgement fixtures preserve the original and converge to one committed event after a fresh explicit review. Failed retry retains its original; an owned stalled helper terminates when its synthetic parent dies. Ordinary artifacts exclude fixture/crash markers. The maximum source receipt is also replayed with the actual isolated retry helper; the near-1-MiB binding fixture checks digest-selected exact recovery without resending its large original.

Executed evidence:

- Windows/MSVC focused CTest **12/12** passes: retry/crash **32/56**, query/lifecycle **74/20**, final extended pending/crash **97/198**, source/storage **486/32**, legacy billing/recovery **90/41** and build/document checks.
- Linux/WSL/direct GCC: retry/crash **34/56**, query/lifecycle **76/20**, final pending/crash **97/206**, source/storage **486/32** and legacy billing/recovery **90/41** pass. Linux own-code/helper/probe **ASan+UBSan** passes that selection, including final rebuilt maximum-source/large-binding recovery cases. Existing energy dependencies are reused from the previously instrumented build; the two extra process checks and eight extra pending checks remain Linux/POSIX-specific.

This is fixture/CPU/WSL evidence, not physical storage failure, deployed trust/lifecycle, real billing sources or native activation. No new UI/PTY or named-timezone qualification is claimed. Large-input creation/submission staging, native review/background ownership, cycle/archive management and real-source/rule qualification remain unfinished.

[P-05n](billing-input.md) subsequently adds separate large-input staging and explicit CreateNew/Submit. Its Issued state never substitutes for this helper's reviewed exact-retry authority.
