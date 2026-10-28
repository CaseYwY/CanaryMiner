# Durable pending intent and restart reconciliation (P-07c)

**Fixture-qualified core only; no production financial activation.** This extends the [P-07b isolated owner](credit-owner.md), not source qualification. The production supervisor still constructs no credit owner, actual-income/forecast/net rows remain disabled, and no account, collector, import/export command, mining authorization or recovery UI is introduced.

Requirements: R-INC-001/003–007, R-SAFE-001/002, R-PRIV-002. Modules: M-LEDGER/M-SUPERVISOR/M-PRIVACY. Test families: T-LEDGER, T-INTENT, T-PRIVACY. All remain partial for their full product scope.

## State and ownership

`CreditLedgerOwner` now defaults to `CreditOwnerPolicy::persistentRecovery=true`. All journal reads, flushes, replacement and SQLite work happen in its **native helper**, not in `submit`, `requestStop`, snapshot or render callbacks. Queue admission still means volatile admission, not persistence or a source-cursor acknowledgement. The producer must retain every unacknowledged batch and obey backpressure.

One bounded sidecar, `<ledger>.credit-recovery.json`, retains the most recent original request and its state. It is installation-local private data, including the path, provider/scope binding, IDs, amounts and original timestamps. It is not diagnostic text or a public fixture. A separate OS-owned `.lock` lease covers the entire journal-plus-ledger operation; lock order is journal first, ledger second. The owner retains no idle database/journal lock.

| State | Meaning | Admission after explicit start |
|---|---|---|
| Empty | No sidecar entry | Ordinary fixture submissions allowed after preflight |
| Prepared | Durable original input precedes an attempted ledger mutation; its final outcome is not established | Read queries allowed; new batches blocked; explicit exact retry only |
| Confirmed | Helper observed `Ok`/`Replay` and persisted a receipt for that exact input | Must first pass a **read-only latest-batch proof** against the current ledger |
| Rejected | A new submission received a definite non-writing rejection and that result was persisted | The owner faults; a later explicit start may review it and submit corrected source input |
| Diverged | A structurally valid confirmed receipt does not match the current latest ledger facts | New batches blocked; no automatic replay, cursor inference or destructive repair |
| Unavailable | Invalid/newer/corrupt/inaccessible/mismatched journal, lock or store | Fault; preserve files; no fabricated empty history |

`durableSequence`/`durableSnapshotId` identify the retained entry, **not necessarily a committed source cursor**: inspect state/status and the acknowledgement. Previously returned immutable snapshots remain unchanged. A new owner/process may use a fresh data generation and still find the sidecar; data generations never authorize mining.

## Write ordering and lost acknowledgements

For a new persistent submission:

1. Validate the bounded fixed contract and request before creating a journal. Acquire the journal lease, read/validate any prior entry, open/revalidate the existing ledger under its own lease, and refuse unresolved prepared/divergent input.
2. Persist **Prepared**, containing the original request, before calling `CreditLedger::apply`. A failed intent write never authorizes an apply. A surviving entry never authorizes recreating a missing ledger as a new empty store.
3. Apply the batch with the existing atomic SQLite schema-v1 transaction. It remains possible to stop/crash after COMMIT.
4. For `Ok` or exact `Replay`, persist **Confirmed** before returning success. For a first submission's definite non-writing failure, persist **Rejected**. `Storage`/`StaleBatch` remain unresolved. A failed receipt write is conservative/uncertain even if the ledger already committed.
5. Return the retained record and typed query totals through bounded IPC. Lost response bytes cannot turn the durable facts into rollback. No later queued job is silently flushed on stop/fault.

There is no two-file atomic transaction claim. Instead, **Prepared must become durable before the ledger can change**. Crash before that point cannot have applied this request. Crash after intent but before receipt leaves the exact input for explicit reconciliation. Crash after the receipt but before the parent receives it leaves a durable receipt whose current-ledger proof can be inspected without reapplying anything.

`CreditLedger::verifyLatest` is a new read-only proof: normalize/compare the supplied source snapshot with the current latest canonical batch in a read transaction. It never inserts a missing batch. Only exact `Replay` proves the latest snapshot; an advanced cursor (`StaleBatch`) alone is not proof of old content. A valid receipt with a missing/rolled-back/differently advanced database is not permission to recreate, auto-replay or report complete zero income.

For **explicit** `retryUncertain()`:

- Replay the retained original identity, record/coverage order, values and source/observation instants. Only request generation/ticket, query horizon and invocation time change. A new discovery time, altered amount or other scope is not the same recovery input.
- `Ok` or exact `Replay` plus a successfully persisted confirmation can clear uncertainty. No original timestamp is freshened, no quantity is doubled, and a stale rolling query remains partial.
- A failed retry does **not** prove that the old attempt never committed. Conflict, clock, sequence, storage or stale failures cannot turn a prepared entry into a rejected/cleared one. The original survives another process restart. There is no automatic discard, database rebuild, source substitution or retry loop.
- Existing in-memory uncertainty is not cleared merely by a new preflight query. A surviving confirmed receipt can be independently verified by a newly created owner; an existing uncertain owner still requires its explicit retry path.

Ordinary `CreditLedger` and explicit `persistentRecovery=false`/volatile process requests retain the earlier synchronous/volatile compatibility API. They do not become durable collectors by using FULLMUTEX. Mixing independent legacy writers with a pending journal can make reconciliation impossible without source/ledger review; a later cursor is not silently accepted as proof. The production app activates neither compatibility nor persistent ingestion yet.

## Encoding, bounds and local storage

- Persistent requests use strict `CMCR2` and replies `CMCA2`; volatile compatibility stays `CMCR1`/`CMCA1`. Mode, exact array shapes, enum/time/type bounds, generation/ticket, source binding, original input and acknowledgement consistency are checked. There is no public HTTP request, shell, SQL or executable supplied by the record.
- Persistent **whole request <=48 KiB**, leaving room for retained original batch plus at most four totals in the existing **64-KiB reply**. The old volatile request bound remains 256 KiB. Existing record-count/identity/scale/coverage bounds still apply. Oversized input is rejected before queue admission; never truncate a provider page, invent pagination or split completeness claims to get around the limit. A real source must qualify an appropriate bounded delivery contract.
- Sidecar schema `CMCJ1` stores the original canonical volatile request, state, optional apply result and SHA-256 corruption digest; it is at most 48 KiB + 256 bytes. It reuses the existing host hash implementation as a corruption checksum, **not authentication, encryption or a new TLS primitive**. Unknown versions, extra shapes, noncanonical bytes, checksum damage, nonregular targets and path/contract/limit mismatches fail closed without rewriting.
- Writes use an exclusive same-directory temporary file, `fflush` plus file `fsync`/`_commit`, then atomic replacement. POSIX also checks `fsync` of the containing directory before proceeding to apply; Windows uses replacement with `MOVEFILE_WRITE_THROUGH`. New POSIX files are 0600; Windows inherits the protected parent ACL. Locks are OS-owned and release on process exit; an existing lock file is not a stale-lock permission to delete another writer's state.
- A protected, already provisioned **local** data parent is required. This is not a sandbox against hostile local directory writers, a guarantee that a drive/mount is local, or certification of physical-power-loss/drive-cache/ancestor-directory durability. Orphan temporary files from an interrupted pre-replacement write are not applied inputs and are not automatically deleted by a later process.

The original local process cancellation, minimal environment/descriptor inheritance, finite parent IPC polls and unreaped-client fault boundary are unchanged. Real kernel/D-state, loader/spawn/reaping and parent-death helper quarantine remain release gates; no userspace timeout proves hard OS deadlines.

## Migration, recovery and rollback limits

No credit SQLite schema, accepted history, UI/runtime/manual-market schema or mining config migrates. Missing sidecar is initially Empty; loading it does not invent provider history. Its stored path, source contract and limits are immutable for that retained input. Moving just the sidecar, changing the binding/limits or independently restoring one file is not a supported migration; preserve the bundle and obtain explicit recovery/source review rather than editing the digest or deleting it.

Rolling back code does not remove a sidecar or undo committed facts. Do not run an older/volatile collector to bypass pending recovery, and do not delete the sidecar/ledger as a repair. Actual source recovery/import/export and native review controls remain unfinished; this internal core is not a user-facing production importer. Pending records are local sensitive files ignored by Git, not material to upload for debugging.

## Validation scope

All data is invented; no pool/account or miner is contacted. Unit/process tests exercise bounded canonical requests/receipts, no-auto-apply inspection, damage/unknown/mismatched storage preservation, sidecar leases, timestamp identity, exact retry, rejected new input versus failed retry, stale/conflicting independent writers, missing/divergent ledger detection and owner destruction/recreation with prepared/unsettled data.

The native crash probe exits a separate process with `_Exit` while holding the journal lease at **Prepared**, **ledger applied before receipt**, or **Confirmed**. A new process inspects without mutation, explicitly retries once, and retries again without doubling or freshening facts. This proves those userspace process-exit boundaries and OS lease release, **not** abrupt death of a live parent plus its unreaped/stuck helper, genuine power loss, filesystem corruption repair or native-platform production GPU behavior.

Executed evidence (2026-10-28):

- Windows/MSVC: **132 journal checks**, **27 separate-process crash/restart checks** and final focused CTest **42/42**, all passing. Five repeated credit-owner runs also pass. The selection retains prior ledger rollback/integrity, market, exact arithmetic/oracles, energy/time, donation, history, shutdown, intent, settings and launchers.
- Linux/WSL/GCC: **133 journal checks**, **27 separate-process crash/restart checks**, **65 process checks** and **62 owner checks**, all passing. The extra journal check covers a symlink target without assuming unprivileged Windows symlink support. Rebuilt own core, ordinary/fixture helpers and process/owner/journal test programs pass ASan+UBSan; the system SQLite runtime is not instrumented. Linux host builds here use direct GCC commands, not Linux CMake qualification.
- Linux full Python discovery: **121 tests, OK with seven fixture-dependent skips**. These skips are not assumed native-executable passes. Import guard and architecture contracts pass; native Windows full Python discovery/toolchain/encoding limitations are not requalified.
- Ordinary Windows helper/library and Linux helper/recovery object scans contain no private stall/crash fixture seams. Source/index/history privacy scans use the untracked private deny-list; no binary, journal, database or raw runtime evidence is staged. No new dependency is downloaded or bundled.

Review added explicit regressions for failed-retry non-erasure, read-only confirmed-receipt proof, missing/divergent databases, checksummed-but-noncanonical input and a request immediately below the persistent wire limit whose full receipt still fits the response bound. None of these checks is a live provider or full release qualification.

```text
cmake --preset tests
cmake --build --preset tests --target canary_credit_worker credit_worker_fixture test_credit_journal credit_journal_probe test_credit_process test_credit_owner
ctest --preset tests -R "^credit_" --output-on-failure
```

Real source/terms/attribution, native recovery/import/export UI, persistent collection/source cursor integration, helper packaging/authentication, OS failure qualification, today's forecast/net integration and all production release gates remain open. No installed miner is replaced and `FinancialView.enabled` remains false.
