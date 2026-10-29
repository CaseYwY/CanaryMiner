# Journal-aware isolated power queries (P-05i)

M-ENERGY/M-TARIFF; R-ENG-001, R-TAR-002/003/005, R-SAFE-001/002 and R-REL-001 remain partial; T-ENERGY/T-TARIFF/T-INTENT/T-PRIVACY; D-48. This isolates [exact power snapshots](energy-trace.md) behind a one-shot native helper and composes the existing [recovery journal](energy-recovery.md).

## Read admission and recovery

`EnergyTraceProcessClient` accepts only Existing + Inspect, no batch, exactly one ordinary half-open time query and a 1–512 output-interval budget. It takes an explicit installed helper path. It adds no retry, store discovery, sampling, periodic owner or ordinary runtime activation.

The helper takes the journal lease before the ledger lease, using `EnergyRecoveryOpen::QueryOnly` and SQLite `ExistingQuery` (READONLY). QueryOnly rejects initialization, preparation and settlement while preserving the inspected state. Both original files must exist and audit successfully. The cooperative lock files and SQLite WAL-reader bookkeeping may require companion-file access; this is not a no-filesystem-write or hostile-path sandbox.

The source reply retains the committed checkpoint, exact totals and complete recovery original. Only a consistent Empty, Confirmed or Rejected state can supply a complete power trace. Prepared returns Conflict **even when the exact batch is already committed**; the read does not settle uncertainty. Diverged also returns Conflict. Missing/corrupt/unreadable state and attribution/clock failures preserve originals and supply no trace. Rejected state can expose the proven prior committed prefix, never the rejected batch. An empty source remains unavailable rather than measured zero.

Totals, recovery proof and trace come from one leased audited model. Later source activity cannot mutate the returned value. This query receipt is not a monetary commit, a fresh collection session, or permission to retry an observation.

## Bounded wire and lifecycle

- `CMETQ1` contains parent identity, maximum intervals and the existing structured `CMEQ2/3` Inspect request. Total request limit: **48 KiB**. New-store/write requests, register queries and old write-IPC mode widening are rejected.
- `CMETA1` contains an explicit byte-length-framed `CMEA2/3` source reply, trace status, and a canonical interval array or null. Total response limit: **64 KiB**. Recovery originals stay structured arrays under the existing per-string JSON bound.
- Validation binds generation/ticket, recovery/head relationship, requested interval, coverage and observation time. A full trace must be contiguous, normalized, nonnegative, precise to integer milliwatts, within the selected-channel physical bound, and integrate exactly to the same source energy total. Adjacent equal-power rows, gaps, different evidence, fabricated integral, trailing data and oversized responses reject.
- Partial coverage stays partial with no complete trace. More than the requested number of distinct intervals returns Capacity without averaging or publishing a shortened trace. Unknown transport outcomes produce no valid reply.
- The existing `LocalTaskProcess` supplies one flight per client, cancellation, a caller deadline within 50–60,000 ms, bounded termination/reaping and Unreaped reporting. Request data travels through private local IPC, not process arguments. Call this synchronous client from background ownership, not the renderer or stop path.
- Windows uses the launcher's non-inherited KILL_ON_JOB_CLOSE job; Linux checks the actual parent PID before and after setting PDEATHSIG. Linux rejects a substituted parent PID. Windows relies on its owned job rather than using the request PID as authentication. Unsupported platforms fail parent binding.

The helper is a development CMake target. No new default runtime factory, production install/dependency trust decision or financial display is enabled. Global quarantine, actual uninterruptible kernel/disk stalls and other helpers' lifecycle remain separate qualification work.

## Verification

Targets: `test_energy_trace_process`, `canary_energy_trace_worker`, `energy_trace_query_fixture`; CTest `energy_trace_process` and `energy_trace_lifecycle`. Synthetic tests exercise Empty/Confirmed/Rejected/Prepared/Diverged, COMMIT before receipt, explicit later recovery, journal write refusal, both source files, source preservation, malicious wire, single-flight Busy, cancellation, timeout, wrong worker and parent death.

Both source schema versions round-trip **64 channels, 512 chronological intervals and a retained 128-reading/32-window original together**, using maximum-length identifiers, large valid clocks/counters and maximum milliwatts. The 513th distinct interval refuses. Lifecycle tests terminate only their own synthetic parent, observe its owned stalled child, then prove a fresh read and unchanged source/journal. Ordinary helper/library artifacts are checked for absent stall/extra-output fixture seams.

Executed evidence:

- Windows/MSVC focused CTest **18/18** passes. New process/lifecycle suites pass **638/10** checks; rebuilt history/recovery/compaction, capture/authorization, collector/runtime and build/document checks pass in that selection.
- Linux/WSL/direct GCC: new process/lifecycle **640/10**, trace/storage **6,329/54**, history/recovery/compaction **746/359/3,674**, crash probes **73/149/111**, authorization/reconstruction **1,481/124** and capture **1,392** pass. Two extra process checks exercise the Linux PID-mismatch rejection.
- Linux own-code/helper/probe **ASan+UBSan** passes the same selection and counts. The initial Windows build found a missing test-only `<iostream>` include; it was added before the successful focused runs. No production parser or warning policy was weakened.

These are CPU/fixture/WSL tests, not real sensor/cadence/storage-failure/production deployment evidence. Source-to-monetary durable receipts, pending monetary originals, bounded compute ownership, runtime configuration and real source eligibility remain integration work.

[P-05j](billing-source.md) subsequently adds optional anchored CMETQ2/CMETA2 queries for source-to-monetary continuity. An unprovable expired or mismatched earlier source head blocks a complete trace; no-anchor v1 calls keep the contract above.
