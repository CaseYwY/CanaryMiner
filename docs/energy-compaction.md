# Exact energy checkpoint compaction (P-05f / P-08e)

This adds an **explicitly opted-in SQLite v2 store** to [energy history](energy-history.md). Closed per-channel power intervals are folded into a bounded, exact piecewise aggregate; older raw batch detail expires atomically with the checkpoint. Existing arbitrary-millisecond energy and reset-window queries retain their exact quantities and coverage. No time-bin average, interpolation or rounded subtotal replaces the observations.

Requirements R-ENG-001, R-TAR-003, R-SAFE-001/002, R-PRIV-002 and R-REL-001 remain partial; M-ENERGY/M-TARIFF; T-ENERGY/T-TARIFF/T-INTENT/T-PRIVACY/T-RELEASE; D-39. Native persistence consent/identity/retention controls, producer batching, real-workload storage qualification, monetary billing and production activation remain integration work.

## Exact retained projection

Each compact segment stores half-open UTC start/end, summed integer milliwatts and the number of covered channels. It describes the **same fixed source/basis/ordered-channel binding**, not a new whole-system or incremental measurement.

A sweep over interval boundaries sums power and coverage independently. Adjacent segments merge only when both values are identical and there is no gap. Covered zero-power time remains covered; missing time creates no segment. Queries clip segments at their actual millisecond edges, then use the existing exact `mW * ms * 0.001 / 3,600,000,000` arithmetic. Counts multiply elapsed time to preserve device-ms coverage, including partial multi-board coverage.

The checkpoint also preserves the current session/UTC-monotonic anchors, each channel's last optional raw reading, global continuity/wall/monotonic guards, every used session ID and all independent reset windows. A channel's late next sample can close an interval that overlaps another channel's already compacted time; the next sweep combines them exactly. Compaction itself does not advance coverage, hold a missing tail, renew observation times, start a session or reset a register.

All existing aggregate queries, including backdated explicit reset windows, remain exact over the retained sample-and-hold projection. This is **not lossless raw-detail archiving**: the original per-channel observation sequence and old batch bodies are not recoverable from summed segments. There is no per-board historical re-attribution API. Changed channel selection still requires a separately bound store.

## Explicit format and identity contract

`EnergyHistoryLimits::compaction` is absent by default. A supplied `EnergyCompactionPolicy` selects v2 for **CreateNew** and must match exactly for Existing. There is no v1 migration, adoption, rename or in-place policy change. The ordinary supervisor does not construct this owner or infer consent from its in-memory energy preference.

V2 batch IDs are exactly `energy.<decimal sequence>`. The sequence is local to the immutable store binding, starts at 1 and cannot be reused as a new append. This avoids an indefinitely growing historical batch-ID set while rejecting alias/reused IDs after raw expiry. Measurement session identities remain caller-provided, explicit and non-reusable; they are not generated from batch sequence or owner generation.

Application ID stays `0x434d4548`; `user_version` is 2. The original `meta` and `events` tables retain their exact definitions. One additional `retention` row stores:

- immutable `CMEP1` policy and storage budgets;
- canonical structured `CMECS1` checkpoint, or empty content before the first compaction;
- a SHA-256 seal binding the checkpoint to the source binding and policy.

The checkpoint records lifetime counts, the original observation-chain head, a canonical aggregate segment index, active continuity state, session keys, reset windows, and the **latest complete original batch plus its prior chain head**. The latest original is retained so [durable pending/receipt reconciliation](energy-recovery.md) still has exact content proof at a compaction boundary.

New append, checkpoint replacement, old `events` deletion and `meta` update use **one SQLite WAL/FULL transaction**. Failure before COMMIT preserves the old prefix; failure after COMMIT is still an uncertain acknowledgement and is reconciled through the existing Prepared journal. A compacted point preserves the original chain head rather than pretending the expired raw chain can be re-derived. Its sealed aggregate state is the new audit baseline; retained subsequent events are replayed and chain-checked normally.

Open validates the exact schema/policy, one-row checkpoint presence, seal, canonical encoding, sequence/count bounds, source/time/clock/session consistency, original/head relationship, original-to-active-tail agreement, reset key/overlap rules and sorted nonoverlapping maximally merged segments. Seals detect accidental corruption, not an adversary who can coherently rewrite the directory and recompute them. Original expired observations cannot be independently re-audited from this derived baseline.

## Raw detail and recovery are separate

`EnergyHistoryCheckpoint::compactedSequence` is the latest atomic compaction point, not the current committed sequence. Raw originals **before** that point have expired. The original **at** the point and all later event bodies are retained.

Expiry is logical API/detail retention, not secure erasure of SQLite free pages, WAL files or external backups.

- An exact retained original can return `Replay`; a different retained body returns `Conflict`.
- An earlier expired original returns **DetailExpired**, never guessed Replay/Ok or a new application. This status makes no claim that supplied old content matches an expired body.
- Numeric committed progress, aggregate energy, coverage, session/window counts and current receipt proof survive expiry.
- New parent generations still require a fresh sampling session. Restart only inspects; Prepared input still requires generation/revision-bound explicit retry.

Persistent v2 operations use `CMEQ3`/`CMEA3`, carrying the explicit policy and compaction boundary, plus `CMEJ2` for the private original-input journal. Explicit volatile v2 requests also use v3 IPC with a null recovery snapshot. Legacy `CMEQ1`/`CMEA1`, `CMEQ2`/`CMEA2` and `CMEJ1` retain their old meanings; a lower-version reply is not accepted for a v2 request. IPC remains 64 KiB and original batches remain 48 KiB.

## Bounds and retention meaning

Default compaction trigger is 512 batches, selectable from 1 through the configured raw-batch budget (at most 4096). The existing reading budget, at most 131072, triggers compaction when reached as well. The staged model can temporarily include one additional bounded input; no over-budget uncompacted prefix is committed. A single batch must still fit the configured reading budget.

The exact segment budget is 1–32768 (default 32768). Session and reset-window budgets remain lifetime bounds (at most 1024 and 4096). Sequence is bounded at 10^12 and lifetime reading counts at 128 per sequence to keep admission/coverage arithmetic bounded. Database page budget remains 1–128 MiB (default 64 MiB); page reuse after expiry does not promise physical file shrinkage or a total filesystem/WAL quota.

Local checkpoints are at most **4 MiB**. Their dedicated ASCII-array lexical boundary permits at most 250000 nodes, depth 16, 256-byte strings and 32-byte literals before using the existing strict JSON grammar. Public/provider JSON limits and the common 1-MiB parser entry point are unchanged. These internal files never travel through the 64-KiB IPC.

Capacity exhaustion rejects the entire append and preserves prior exact facts; it does not silently downsample, drop older energy, reset counters or clear the pending record. Segment growth depends on changes in summed power and coverage. **This is bounded exact compaction, not guaranteed indefinite or 30-day retention at every real sampling cadence.** The long synthetic trace proves reuse for compressible observations, not a storage-duration promise for variable GPU/game telemetry. Production cadence/budget selection and a future explicitly labelled archival policy remain to be qualified.

## Verification

The native suite checks exact arbitrary-edge integrals against independently enumerated intervals, randomized power/missing/continuity/session changes, late per-channel closure, reset projections, expired/retained replay, schema/policy refusal, segment-cap atomicity, helper/owner reconstruction at all three durability boundaries, and strict v3 replies. It includes:

- a **64-day synthetic constant-power trace** with 1441 batches / 184448 readings, exceeding the configured 8-batch / 1024-reading raw budgets while retaining exact all-history and clipped rolling queries;
- a full **32768-segment checkpoint**, including covered zero-power intervals, persisted and reopened;
- windows-only and session-start-only checkpoints with unavailable energy rather than fabricated complete zero.

Independent Python checks abrupt exits before/after the compaction transaction, original-event deletion only after commit, exact energy and device-ms sums, reset preservation, rechecksummed semantic corruption, missing/wrong schemas and fixture-seam exclusion from ordinary artifacts. Fixture death does not certify physical power loss, real storage/kernel stalls or live-parent orphan quarantine.

Executed evidence (2026-10-29):

- Windows/MSVC focused CTest **53/53**, including prior history/recovery, credits/cursors, session energy, tariffs/market, donation, stop/intent and build/import/document contracts. New compaction program **3674 checks**, also passing five consecutive repetitions.
- Linux/WSL/GCC: **3674 compaction checks** and five repetitions, **111 independent compaction crash/integrity checks**, existing energy recovery/history **359/746**, prior history crash **73** and recovery subprocess **149**. Own core, checkpoint codec, ledger, process, owner, transport, helper and crash probes pass **ASan+UBSan** for the compaction, recovery and history native suites plus the compaction/recovery subprocess suites, including the maximum 32768-segment case. Linux builds use direct GCC with warnings as errors; CMake remains unavailable there.
- Both platforms' **111-check** independent compaction suite passes. Linux Python discovery remains **123 tests, OK with seven explicit fixture-dependent skips**; final architecture contracts **4/4**. These are CPU/fixture results; no new native UI or physical sensor/long-run workload is qualified.

```text
cmake --preset tests
cmake --build --preset tests --target test_energy_compaction energy_compaction_probe canary_energy_history_worker energy_recovery_worker_fixture
ctest --preset tests -R "^energy_(compaction|history|recovery).*$" --output-on-failure
```

The initial maximum-size test exposed the shared network JSON node/byte cap and common parser entry-point cap; only the local checkpoint now uses the separately bounded grammar entry. An independent window comparison was corrected to compare key-ordered data, and a missing retention row now reports integrity unavailability rather than binding mismatch. No default-on runtime retention, installed-miner replacement, real sensor/game/GPU qualification, monetary bill or income forecast is supplied by this batch.
