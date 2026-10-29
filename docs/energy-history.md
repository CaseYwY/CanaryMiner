# Isolated observed-power history and independent reset windows (P-05d / P-08c)

**Development library/helper, not production persistence activation.** This adds an audited, bounded SQLite history of raw power observations and independently named reset windows. It supports exact window projections and explicit cross-session recovery. The ordinary supervisor still uses the [in-memory session collector](session-energy.md); its collection preference is not consent to retain raw observations on disk. [P-05f/P-08e](energy-compaction.md) adds separately opted-in v2 exact compaction. Native persistence consent, stable device binding, retention-duration qualification, recovery UI and production helper packaging remain integration work.

[P-08f](energy-capture.md) supplies the optional producer/supervisor seam, private namespace-based selected-device mapping and native recovery page. The ordinary factory is still empty pending saved persistence consent/retention, secure identity provisioning and selected-set/store enrollment; mapping/review alone does not activate storage.

Requirements: R-ENG-001, R-TAR-003, R-SAFE-001/002, R-PRIV-002 and R-REL-001 remain partial. Modules: M-ENERGY/M-TARIFF. Tests: T-ENERGY/T-TARIFF/T-INTENT/T-PRIVACY/T-RELEASE. D-37 defines this slice. It supplies neither attributable income nor a bill, forecast, tariff activation or hardware-control permission.

## Binding and observation identity

`EnergyHistoryBinding` is immutable for one store: power basis, origin, adapter, private scope, ordered private channel keys and the declared local-history lower boundary. It is not device discovery or proof of lifetime coverage. Channels must be distinct, with 1–64 entries. All identifiers are bounded ASCII keys (1–128 characters), not terminal text, account credentials or executable paths.

- `SyntheticFixture` explicitly uses a `fixture.` adapter; tests may exercise each known power basis. These inputs never certify hardware measurements.
- `ObservedTelemetry` is restricted to `GpuBoard` and `native-nvml-milliwatts`. This is a provenance assertion by the caller, not sensor authentication/calibration. There is no real wall/incremental/meter adapter in this batch.
- `DeclaredForecast` and unknown origin/basis are rejected. [Declared whole-meter openings](tariff-declaration.md) cannot be imported as observed quantities. Zero cannot be used as an invented starting balance.
- Caller-provided channel keys must stay bound to the same selected physical scope. The old `selected-gpu-boards/session-<generation>` label is not automatically adopted as persistent identity. A changed selection/source/basis requires a separate binding/store; combining different stores has no implemented attribution contract.

Each parent measurement lifetime supplies a fresh explicit `EnergyHistorySession.id`, UTC anchor, monotonic anchor and maximum hold gap (1–60000 ms). Session IDs may not be reused within the retained store. The library does not generate IDs from a GPU model, PID, owner generation or wall timestamp. A new session seals the previous monotonic domain; its anchor must not precede the prior mapped horizon or observed wall sample. Startup, inter-session gaps and the old session's final tail remain unavailable.

The public owner will not continue a stored session after a new explicit generation starts: its local session authorization is cleared. A new session-start batch is required before admitting fresh readings. The lower-level synchronous/helper API supports consecutive batches within an explicitly supplied current session; its per-operation helper process lifetime is not the measurement lifetime.

## Exact energy and coverage

Readings retain channel, wall UTC, monotonic milliseconds, continuity epoch and optional unsigned integer milliwatts. Missing power is `null`; valid zero is numeric zero. Within each session:

1. Global monotonic and wall readings must not regress; same-channel monotonic time must strictly increase. Different channels may share one timestamp.
2. Mapped UTC is session-anchor UTC plus monotonic elapsed time. Supplied wall UTC must remain within +/-2000 ms and mapped UTC cannot exceed the batch's immutable observation time. A rejected clock batch commits nothing, including earlier otherwise-valid readings/windows in that batch.
3. Hold the previous available power only to the next same-channel reading, when the interval is within the maximum gap and the continuity epochs match. A missing current sample ends the prior hold but starts no new one.
4. Advance the global continuity epoch after **any unrecorded sample or dropped/rejected producer batch**. The next committed reading cannot bridge that gap. This is a caller contract, not detection of samples never supplied to the API. The future telemetry bridge must preserve it.
5. Exact numerator is `milliwatts * elapsed_ms * 0.001`; kWh is that W·ms numerator divided by **3,600,000,000**. Clipping at query/reset boundaries integrates the held power over the clipped duration, with no rounded fraction of a rounded subtotal.

All energy queries use **half-open `[beginUtcMs,endUtcMs)`** intervals, unlike posted-credit `(from,through]` record membership. Requested coverage is `(end-begin) * selected_channel_count` GPU/device-ms, including prehistory, inter-session gaps and unobserved tails. Covered time is the sum of eligible per-channel intervals. `Complete` means complete coverage by this explicitly bounded sample-and-hold model, not a continuous/accurate wall measurement. No covered interval gives `Unavailable` and no complete quantity, even though the internal known-subtotal accumulator is zero. Observed zero with full covered time is a valid complete zero.

Board observations include idle/display/game/donation consumption; they do not identify miner-only incremental energy. No automatic whole-PC, household, baseline, tariff-cost or net-profit conversion is performed.

## Independent reset clocks

`EnergyResetWindow` persists an immutable `(registerId,periodId)` with rule-version key, explicit UTC start/exclusive end and explicit UTC or whole-minute fixed STANDARD-offset provenance (+/-18 hours). Each window is at most 370 elapsed days. No OS timezone, named-zone fallback, recurring-month guess, tariff lookup or supplier certification is involved.

For example, one billing register may have adjacent short periods while an annual-tier register overlaps both. Windows in **the same register** cannot overlap or reuse a period key. Windows in **different registers** can overlap intentionally. Unconfigured gaps remain gaps; there is no inferred reset boundary. Rule versions can change between windows without rewriting earlier observations or allowing the same period key to change meaning.

Crossing a reset boundary changes the projection range, **not stored observation facts or lifetime history**. Explicit-window queries integrate the same raw covered intervals independently. A reset is not deletion, a fresh household allowance, measured meter opening, accrued monetary subtotal or utility settlement. Partial coverage stays partial. A billing query must match the persisted window and its requested clipped bounds; a wrong period/start is rejected rather than silently selecting another window. Current interfaces accept known window keys; native discovery/edit/review of these measured-register bindings is future work.

Composite bill state, annual reconciliation, demand/export/prepaid registers, automatic civil/DST period generation, plan-specific rounding/proration and tariff-currency FX are still separate capabilities. This batch does not turn a GPU-board register into the whole-meter `BillOpening` required by the tariff core.

## Atomic persistence, audit and recovery

`EnergyHistoryLedger` is a synchronous single-owner API. The new `canary_energy_history_worker` confines open/audit/append/query/close to a one-operation native helper. It reuses the existing optional OS SQLite >=3.31 ABI, cooperative OS lease and WAL/FULL transactions. No SDK/SQLite package is downloaded or bundled. New POSIX data files are 0600; Windows uses the protected parent directory's inherited ACL. The protected installation/data parent and cooperative single-writer assumptions still apply.

SQLite application ID `CMEH` (`0x434d4548`), schema v1:

- `meta`: exact immutable binding, committed sequence and chain head.
- `events`: consecutive sequence, unique batch ID, bounded canonical `CMEB1` original input and SHA-256 chain digest.

The `CMEH1` binding is the initial chain seed. Observation batches and reset declarations share **one transaction** with the committed sequence/head. Open checks application/schema identity, exact table definitions, extra tables/views/triggers, SQLite integrity, canonical input, full event sequence/hash chain, session/clock/channel constraints and reset-window consistency. Derived intervals/coverage are recomputed from the raw input. SHA-256 detects accidental corruption, not an attacker who can rewrite the directory and recompute hashes. The live single-owner object caches this audited model; bypassing its cooperative lease is outside that contract.

Exact replay of **any retained committed sequence** returns `Replay` without changing quantities, original observation time or windows. Same sequence with different content is `Conflict`; missing sequence is `SequenceGap`; reused batch/session/period identities are refused. A new append stages the model before the SQLite transaction, so partial validation cannot advance cached facts. COMMIT failure makes the object unavailable; it never publishes guessed success.

`Existing` is the default open mode and does not create a missing database. `CreateNew` must be explicit and refuses every existing target, including empty/corrupt files. No migration, deletion, overwrite, rollback-to-zero or automatic reconstruction is supplied. A partially created file after failed creation is preserved for explicit investigation. No mechanism can detect loss of an entire externally chosen bundle when a caller explicitly asks to create a different new history.

The v1 raw audit is deliberately bounded: at most 4096 batches, 131072 readings, 1024 sessions and 4096 windows; callers may lower these. Database-page budget defaults to 64 MiB, adjustable within 1–128 MiB. Each batch permits at most 128 readings and 32 windows, with a 48-KiB canonical body. Full requests/replies are at most 64 KiB and contain at most four query results: `CMEQ1`/`CMEA1` for volatile compatibility, [P-05e/P-08d](energy-recovery.md)'s `CMEQ2`/`CMEA2` for durable recovery. Limits are admission/storage budgets, not heap/scheduler/disk quotas. Reaching capacity stops admission; **no pruning or indefinite recording claim** is made. Production retention/compaction must preserve totals, reset semantics and honest query/detail coverage before activation.

## Owner and stop behavior

`EnergyHistoryOwner` is explicit-start, single-flight and generation/revision-bound. Start only schedules a preflight open/checkpoint. Submit binds to the observed committed sequence under the same mutex and immediately publishes `Busy`; there is no speculative multi-batch queue. Refresh publishes a read-only fact snapshot, not new observation coverage. Querying can still require SQLite lock/WAL housekeeping; “read-only” describes facts, not a promise of no filesystem activity.

The request and reply carry generation/ticket identity. Reply decoding validates coverage, time bounds, model precision, physical integer-milliwatt upper bounds, complete-versus-partial quantities and checkpoint consistency. The IPC contains no executable/provider command or network instruction. Common local process transport now has its own CMake library, shared with credits without changing its runtime implementation.

On timeout/cancellation/lost or invalid reply, a launched append may already have committed. The owner retains an immutable in-memory original batch, faults, and permits only a new explicit generation to inspect the store. [P-05e/P-08d](energy-recovery.md) now also saves helper-side Prepared input and receipts by default, allowing a new parent to reconstruct unresolved input. An unresolved batch then requires `retryReviewed` with the exact current generation/revision. Successful exact apply/replay clears it; a rejected retry cannot erase old uncertainty. There is no automatic retry, source refresh, worker restart or mining authorization.

`requestStop()` only cancels/discards/revokes publication in memory; it does not flush or wait for storage. `stop()` joins outside the mining/safety/render loop. A late helper result cannot publish or revive collection, and stopping cannot roll back already committed observations. Uncertain creation is inspected as `Existing`; it is never retried as automatic new-file creation.

The original P-05d/P-08c slice could recover only the committed prefix after parent loss. Its [P-05e/P-08d extension](energy-recovery.md) retains a bounded durable original input/receipt, with prefix-bound inspection and explicit exact retry. [P-05f/P-08e](energy-compaction.md) adds opt-in v2 compaction while retaining the latest original for that proof; older raw detail explicitly expires. Samples never reaching Prepare and the final unobserved tail can still be lost. A fresh owner always requires a fresh measurement session; coverage never treats missing tails as observed zero. Native consent/recovery controls and long-run producer spooling remain follow-up work.

## Validation and integration gates

The fixtures use invented board keys, times and power. Tests cover exact/missing/zero/max milliwatts, independent register boundaries, session reset, same-key conflicts, drop epochs, rollback/drift/gaps, partial batches, replay and observed-time preservation, strict bounded IPC, stale/concurrent admission, read-only reopen, native helper timeout/stop and lost acknowledgements. Independent Python SQLite checks calculate the expected integral and recompute the chain, kill a process before/after COMMIT, and corrupt both raw checksums and semantically invalid **rechecksummed** records. Ordinary artifacts are checked for absent private crash/stall seams.

Executed evidence (2026-10-29):

- Windows/MSVC focused CTest **49/49**, including credit/cursor/recovery, session-energy/settings, exact tariff/market, donation, shutdown/intent and build/import/document regressions. Final structured-envelope/borrowed-query-bound changes pass the **2/2** targeted rerun. The final new program passes **746 checks** and five consecutive repetitions; existing credit-owner/cursor programs also each passed five repetitions after the shared CMake transport extraction.
- Linux/WSL/GCC: **746 checks** and five repetitions; own model/ledger/process/owner/transport/helper/test code passes **ASan+UBSan with 746 checks**. Builds use explicit GCC commands with warnings as errors. Linux `cmake` was unavailable; no Linux CMake configuration/build result is claimed.
- Both platforms: **73 independent process-crash/integrity/seam checks**, including pre-COMMIT rollback, post-COMMIT abrupt exit and exact replay, independently recomputed power/hash chain, rechecksummed semantic corruption, unknown schemas, missing/empty-target preservation and ordinary-artifact seam exclusion.
- Linux Python discovery: **123 tests, OK with seven explicit fixture-dependent skips**. New standalone helper scripts are import-safe. Native Windows full Python discovery/toolchain/encoding and full pinned-verifier/native CUDA/SASS release gates remain unqualified.

New history code is unconnected to ordinary terminal controls; no new PTY UI behavior is claimed. Existing native terminal/collector evidence remains separate from this library/helper evidence. The initial Windows compile exposed a missing explicit declaration include for the shared JSON guard and was fixed. A large-envelope review caught that JSON-string-wrapping a batch would incorrectly hit the shared 4096-byte string guard; the wire uses structured arrays and tests 64 channels, 128 readings and 32 windows, including an actual helper round trip, without weakening that guard. Staged source/docs/tests pass the local private deny-list and artifact audit; no runtime records or private test paths are contribution files.

```text
cmake --preset tests
cmake --build --preset tests --target test_energy_history energy_history_probe canary_energy_history_worker energy_history_worker_fixture
ctest --preset tests -R "^energy_history.*$" --output-on-failure
```

Still required: stable private physical-channel binding and session-ID creation, explicit native persistence/retention/recovery controls, producer batching/backpressure/continuity bridge, production activation/helper packaging, qualified sensors/baselines, real-source income/FX and today's forecast, accrued monetary billing state, genuine disk/power/kernel/parent-death qualification and the broader P-09–P-14 work. No installed miner, real GPU workload, game, pool, driver reset or reboot is needed by these host tests. The whole planning-book objective remains unfinished.
