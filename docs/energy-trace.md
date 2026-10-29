# Coherent read-only exact power traces (P-05h)

M-ENERGY/M-TARIFF; R-ENG-001, R-TAR-002/003/005 and R-REL-001 remain partial; T-ENERGY/T-TARIFF/T-PRIVACY; D-47. This provides the source-read foundation for monetary integration after [P-05g](billing-ledger.md). It is synchronous core code, not an activated billing adapter, query helper or collection owner.

## One committed source snapshot

`EnergyHistoryLedger::queryTrace(begin,end,now,maxIntervals)` returns an `EnergyHistoryTrace` containing the full private source/channel binding, audited committed checkpoint, exact query totals and an optional complete `PowerTrace`. They come from the **same single-owner cached model**. The object is not concurrently mutable/thread-safe; copy the returned value before handing it to another owner. Later appends cannot mutate an already returned snapshot or refresh its observation time.

Only complete selected-channel coverage of every millisecond in `[begin,end)` can supply `completeTrace`. Partial/empty coverage retains honest known-energy/device-time totals and no complete trace. Missing samples, inter-session gaps, dropped continuity and unobserved tails never become zero or extrapolated power. Observed zero with complete coverage remains an exact zero trace. Query clock/source-time checks match the existing energy model; neither current wall time nor a query ticket is a source checkpoint.

## Preserve chronology across raw and compacted history

The query merges exact aggregate checkpoint segments with retained raw per-channel held intervals. It clips their endpoints, sweeps simultaneous boundary changes, verifies covered-channel counts, and emits the sum of **integer milliwatts converted exactly to watts**. Adjacent intervals merge only when power is exactly equal. The resulting exact integral must match the same snapshot's energy total.

It does not average power across tariff boundaries. Two traces with the same kWh can have different TOU/dynamic costs. Tests feed the source trace into `accrueBill` and independently compare a deliberately flattened trace to expose that distinction; this test is not a runtime monetary import.

The output budget is caller-selected within **1–512 intervals**, default 512. Exhaustion returns Capacity with no partial trace, even when aggregate energy is complete. Consumers must check both status and `completeTrace`; a complete kWh scalar is insufficient for chronological tariff billing. The bounded working set uses at most the existing 131,072 raw intervals plus 32,768 compacted segments and two clipped events per input. No persistent compaction policy, original detail, source binding or reset-window state changes during a query.

The power origin/basis/adapter and ordered private channels remain unchanged. Native NVML-board provenance cannot become whole-meter, incremental-wall or authenticated physical measurement. Fixture input exercising the native provenance contract is still synthetic test evidence.

## ExistingQuery and storage preservation

`EnergyHistoryOpen::ExistingQuery` requires an existing regular compatible v1/v2 database, performs the existing full audit and opens SQLite **READONLY**. It checks current WAL mode instead of changing it and skips write-only WAL/page configuration. `apply` returns Invalid in that mode; `verifyCommitted` can still prove retained originals without applying them. The source lease excludes a concurrent cooperative writer/reader for the open object's lifetime. No implicit wait/retry or absent-file creation is added.

SQLite WAL reader bookkeeping and the cooperative `.lock` can require companion access, so this is not a no-filesystem-write sandbox or a hostile-directory guarantee. Unknown/corrupt/wrong-binding source files stay preserved. All open modes now check logical pages against the configured limit; explicit CreateNew also refuses orphan WAL/SHM/rollback-journal companions before creating a database.

Existing `CMEQ1/2/3` write/recovery IPC continues to reject the new mode. No caller can gain it by silently widening the old wire enum. There is no new ordinary entry-point path, production query helper or automatic billing activation in this slice. The low-level snapshot reads committed source facts only; it does not inspect/settle a pending recovery journal or confer collection/retry permission. Future isolated source reads must compose that recovery boundary explicitly.

## Executed evidence

- Windows/MSVC focused CTest: **16/16** pass. The new trace program passes **6,329 checks** and independent source-storage probes **54**; affected history/recovery/compaction **746/359/3,674**, crash probes **73/146/111**, capture **1,392**, authorization/reconstruction **1,481/122**, collector/lifecycle **371/63** and runtime **109** pass after rebuilding affected targets. Native build recipe and architecture checks pass.
- Linux/WSL/direct GCC: trace **6,329**, independent storage **54**, history/recovery/compaction **746/359/3,674**, and crash probes **73/149/111** pass. Own-code/helper/probe **ASan+UBSan** passes the same selection. The three recovery-probe count differences are existing POSIX permission checks. These are not Linux CMake or new PTY/UI evidence.
- Tests cover asynchronous selected channels, partial versus zero coverage, immutable snapshots, exact arbitrary clipping, raw/compacted equivalence, 512/513-interval refusal, 64 maximum-power channels, unchanged provenance, readonly mutation refusal, old IPC refusal, exclusive leases, missing/orphan/wrong-binding preservation and chronological tariff evaluation. An independent frame oracle constructs expected energy/coverage and full trace boundaries across dropped/absent/zero readings and new sessions. Independent Python SQLite mutations test both schema versions and byte-preserved rejection.

These checks are CPU/fixture/WSL evidence, not real sampling duration, calibrated sensors, certified tariffs, physical-disk/power-loss or production CUDA/SASS/verifier qualification. Source-to-monetary immutable checkpoint binding, durable pending billing recovery, bounded query/compute ownership, notifications/settings and runtime activation remain the next integration steps.

[P-05i](energy-trace-helper.md) subsequently supplies a parent-bound one-shot query helper with QueryOnly journal inspection. Its stricter admission withholds complete traces for Prepared or Diverged recovery even when the low-level committed model alone can supply them.
