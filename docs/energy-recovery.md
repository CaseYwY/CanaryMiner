# Durable energy pending input and restart inspection (P-05e / P-08d)

This extends [observed-power history](energy-history.md) with a bounded private write-ahead input/receipt. A new parent can recover the original **Prepared** batch from disk, inspect its relation to committed facts and explicitly reconcile it. The public `EnergyHistoryOwner` uses this mode by default. The ordinary supervisor still has only its separately enabled in-memory session-energy collector.

Requirements R-ENG-001, R-TAR-003, R-SAFE-001/002, R-PRIV-002 and R-REL-001 remain partial. Modules M-ENERGY/M-TARIFF; test families T-ENERGY/T-TARIFF/T-INTENT/T-PRIVACY/T-RELEASE; decision D-38. Later [compaction](energy-compaction.md), [bounded producer/private mapping/native recovery](energy-capture.md) and [isolated authorization/identity enrollment](energy-authorization.md) extend this protocol. Lifetime-exclusive collector admission, ordinary-runtime binding/lifecycle, monetary billing and production activation remain unfinished.

## Durable boundary

`<ledger>.energy-recovery.json` contains a canonical `CMEJ1` structured array:

- state (`Empty`, `Prepared`, `Confirmed`, `Rejected`), optional applied result;
- **prior committed** sequence and SHA-256 chain head;
- the exact bounded original request, normalized to generation/ticket 1, Existing, volatile format and no queries;
- SHA-256 checksum over the canonical envelope prefix.

For a batch, the stored request's time is its original `observedAtMs`, never the time of restart, review or retry. The request includes the absolute local ledger path, immutable source/basis/scope/ordered channels and storage limits. Copying it to a different path/binding or changing limits is refused. This is private local recovery material, not an export or terminal log. The filename, lease and temporary files are ignored by Git.

The helper takes the recovery lease **before** the ledger lease and holds both through the operation. It atomically replaces a uniquely created temporary file after flushing its content; POSIX also flushes the containing directory. POSIX new journal files are 0600; Windows inherits the protected data directory's ACL. SHA-256 is accidental-corruption detection, not authentication against a writer who can replace the whole directory and recompute checksums.

Normal append order is:

1. Open and audit the journal and SQLite prefix.
2. Save `Prepared` original input and the exact prior checkpoint.
3. Apply the batch using the existing single SQLite transaction.
4. Save `Confirmed` on `Ok`/exact `Replay`, or `Rejected` on an initial definite non-storage rejection.
5. Return a strictly validated checkpoint, recovery snapshot and optional queries.

Queued admission, a ticket and a Prepared target are **not** committed energy. Samples lost before step 2 remain unavailable; this journal is one-operation recovery, not a producer spool. An indeterminate storage outcome preserves the existing durable Prepared record where available. Failed retries never replace it with a new definite rejection.

## Inspection and exact review

`Inspect` never applies or settles a batch. SQLite may perform lock/WAL checkpoint housekeeping; read-only means observation facts and journal contents do not change.

- `Empty` requires sequence 0 and the binding seed in both stores.
- `Prepared` permits either the recorded prior checkpoint or exactly the original batch's resulting checkpoint. The latter also needs read-only exact retained-input proof.
- `Confirmed` requires exactly that resulting checkpoint and retained-input proof.
- `Rejected` requires the recorded prior checkpoint.
- A different/advanced/rolled-back prefix produces `Diverged`; damaged/missing/unreadable material produces `Unavailable`. Neither case rewrites the history or silently accepts another prefix.

The resulting head is `SHA256(priorDigest + "|" + canonicalOriginalBatch)`. Sequence, batch identity, original observation time and full raw content must agree. `verifyCommitted` is read-only; it does not call append to establish proof.

A new owner with Prepared input exposes immutable original content as `NeedsReview`. `retryReviewed(generation,revision)` atomically binds one task to that exact reviewed content. Mutated originals, stale revisions, concurrent second admission and ordinary submits while unresolved are refused. A valid Prepared retry applies once or returns exact replay; any failed retry leaves the old uncertainty intact. A Confirmed retry is proof-only; a Rejected retry returns its saved definite result without applying again.

A freshly reconstructed owner may become Ready from a proven Confirmed receipt, even if the old parent missed that receipt. An existing owner's in-memory uncertain input remains review-required until its explicit successful retry. Neither path grants the new parent permission to continue the old sampling session: new readings require a fresh session-start identity. A divergent record is retained for inspection and cannot be automatically reconciled.

If a request never reached Prepare, only the old parent's in-memory original may exist. The current Retry API requires a matching durable original and therefore cannot silently promote this case into a new append. Manual investigation/explicit recovery tooling for orphaned bundles is still future work.

## Creation, compatibility and bounds

`Existing` requires **both** files, even for zero committed progress. `CreateNew` refuses either existing target. Creation writes the database before its Empty receipt; interruption between those operations leaves a preserved orphan, not an automatically repairable empty history. No migration, adoption, deletion or backup restore is implied. Leases serialize operations, not an entire producer lifetime.

Persistent schema-v1 requests use `CMEQ2` and replies `CMEA2`: explicit Inspect/Submit/Retry mode, audited checkpoint digest and structured original-input snapshot. Old `CMEQ1`/`CMEA1` retain their explicitly volatile behavior. The low-level request defaults to Volatile for compatibility; the public owner defaults to persistence, with an explicit final `persistentRecovery=false` constructor argument for legacy/test callers. Old volatile stores are not automatically adopted by a persistent owner. [Opt-in schema-v2 compaction](energy-compaction.md) uses `CMEQ3`/`CMEA3` and `CMEJ2`, preserves the latest complete original for recovery, and reports expired older raw detail separately from committed aggregate facts.

Request and reply limits remain 64 KiB; journal limit is 64 KiB + 512 bytes. Existing 48-KiB batch, 64-channel, 128-reading, 32-window and four-query bounds remain. Originals are nested **arrays**, not a giant JSON string; the common 4096-byte per-string guard is unchanged. Decode validates mode/batch/open combinations, status/state combinations, original sequence and time, checkpoint/hash relationship, exact reviewed input, model coverage and existing physical/precision bounds. No unversioned downgrade to a volatile reply is accepted.

Stop revokes publication and discards queued memory work immediately; it does not flush the journal or roll back committed observations. Helper termination/join remains outside safety/render paths. A cancelled result cannot republish or revive the owner. A fresh generation inspects instead of automatically retrying creation or replaying retained input.

## Verification

Synthetic tests cover all three interruption boundaries: durable Prepared before application, COMMIT before receipt and Confirmed before the parent receives a valid reply. Separate parent invocations then inspect/review using only files. Tests include exact original/hash/sequence proof; failed/mutated/concurrent/stale retry; zero-prefix and advanced-prefix divergence; missing, corrupt, oversized, noncanonical and wrongly bound journals; both leases; no late publication; fresh sampling identity; and full-size structured helper round trips. Independent Python checks recalculate the envelope and SQLite chain, mutate rechecksummed envelopes and inspect ordinary artifacts for absent fixture seams.

Executed evidence (2026-10-29):

- Windows/MSVC: focused CTest **51/51**, covering energy/history, credit/cursors/recovery, settings, tariff/market, donation, stop/intent, native build/import and document contracts. New recovery program **359 checks**, including five consecutive runs for concurrent admission/stop behavior.
- Linux/WSL/GCC: **359 recovery checks** and five consecutive repetitions, **746 existing history checks**, **73 existing crash/integrity checks**, and **149 independent parent-reconstruction/integrity/seam checks**. The same core, journal, process, owner, transport, helper and probe programs pass **ASan+UBSan**, including the 149-check subprocess suite. Builds use direct GCC with warnings as errors; Linux CMake remains unavailable.
- Windows independent recovery suite: **146 checks** (three POSIX-only file-mode checks account for the Linux difference). Prior energy history remains **746 checks** and the **73-check** crash/integrity suite passes in the focused regression.
- Linux Python discovery: **123 tests, OK with seven explicit fixture-dependent skips**; final architecture contracts **4/4**. There are no new terminal controls in this batch, so it makes no new PTY/native UI qualification claim.

The initial fixture failures exposed an incorrect test assumption that a post-crash SQLite database file must remain byte-identical across WAL checkpointing, and a wrong synthetic binding-array index in the independent corruption probe. Tests now assert preserved logical events and exact journal bytes, with a stable reopen check. GCC also rejected misleading one-line test indentation; the source was made explicit without weakening warnings. These corrections do not relax recovery validation.

```text
cmake --preset tests
cmake --build --preset tests --target test_energy_recovery energy_recovery_probe energy_recovery_worker_fixture canary_energy_history_worker
ctest --preset tests -R "^energy_(history|recovery).*$" --output-on-failure
```

These tests exercise user-space helper exits/timeouts and separate parent reconstruction. They do not certify killing a live parent with a still-running helper, orphan quarantine/reaping, kernel/D-state stalls, physical power loss or disk-controller failures. Native storage/consent UI, device identity, long-run retention, physical sensor/game/production CUDA and helper packaging remain unqualified. No real income, forecast or installed miner is activated by this module.
