# Lifetime-exclusive energy collector and transaction fences (P-08h)

This development slice joins [isolated consent/enrollment](energy-authorization.md) to the [bounded producer](energy-capture.md) with an owned lifetime helper and transaction-time validation. R-ENG-001, R-PRIV-001/002, R-SAFE-001/002 and R-REL-001 remain partial; M-ENERGY/M-PRIVACY/M-SUPERVISOR; T-ENERGY/T-CONFIG/T-INTENT/T-UI/T-PRIVACY/T-RELEASE; D-42.

The ordinary miner still supplies no persistence factory or authorization owner. The new factory is exercised by CPU process tests and the native settings fixture. It does not activate sensors, mining, public providers, actual income or optimization.

## Lifetime and lock ordering

`makeLeasedEnergyCapture` starts one background Load and at most one lifetime-leased Provision. Missing or saved-OFF authorization disables it; CLI suppression, a prior stop, invalid selection or invalid helper paths prevent startup. A failed or lost lease is terminal for this instance. There is no automatic retry, takeover timer, heartbeat expiry, lock stealing or adoption of a recorded old session.

`canary_energy_collector_worker` holds `<authorization>.collector.lock` from before provisioning until process exit. Contenders use nonblocking acquisition; they cannot issue another session while that lock is held. The holder and every guarded history operation also acquire `<authorization>.collector-operation.lock`:

1. Holder acquires lifetime, then operation, then the existing authorization/store locks used by Provision. Existing frozen records must decode, match binding/budgets and not be ahead of the current authorization revision/time before another reservation is written.
2. It reserves a fresh native session through the authorization CAS, generates a separate OS-random epoch and atomically replaces `<authorization>.collector.json` with a canonical `CMELF1` record. The record freezes the issued document, including sampling policy, binding and budgets, plus an outer corruption checksum.
3. Only after releasing operation does it emit one newline-terminated `CMELA1` readiness frame. It then stays alive and silent. The parent validates the complete request-bound receipt before creating the Existing capture with that epoch.
4. An operation acquires operation, probes the existing lifetime lock without creating it, reads the record and checks the epoch before opening the original journal/ledger. It retains operation through the transaction and receipt. A successor therefore cannot publish its epoch during an admitted old write.

An operation admitted before lease loss can finish before a successor is admitted. Stop is not rollback. Conversely, an old request delayed until **after** a successor publishes its epoch is rejected before journal preparation. A successor refused by a still-held operation lock cannot reserve a session and is not automatically retried.

The lifetime domain is the caller's explicit authorization path in a protected local directory. This cooperative protocol does not authenticate against malicious same-privilege writers, directory replacement or copied/aliased authorization records. Unfenced low-level test/recovery APIs still exist; callers must use the leased factory for this guarantee. Deleting lock/fence files is not a supported recovery procedure.

## Frozen write authority and recovery

`CMELQ1`/`CMELA1` handshakes are limited to 24 KiB. `CMELW1` wraps the existing structured history request within its unchanged 64-KiB limit. It accepts only Existing with persistent recovery. It checks the derived database path, immutable binding, all limits, epoch, reservation-time lower bound, and for Submit the current session, batch-reading bound and configured maximum gap. A new sampling session cannot anchor before reservation.

Inspect and Retry remain distinct. Retry may reference an older session only through the journal's exact original/prefix/result reconciliation. Changing the original rejects; an old recovered session cannot become new sampler authority. Journal/database formats remain CMEQ/CMEA/CMEJ v1–v3 as applicable; they gain no epoch, parent PID, raw UUID or reset/rebuild operation. Later consent/cadence saves apply to the next explicit parent and cannot mutate the held frozen record.

No valid readiness receipt means no capture plan. An uncertain reservation retains its original request in memory, with a visible UNKNOWN flag and default-hidden native review. Cancellation during the admitted handshake is conservatively uncertain even if the parent cannot prove that the child reached the write. The stopped snapshot stays frozen; neither late readiness nor Load grants authority. The wrapper exposes no reservation replay/acknowledgement command: the original remains available until destruction, and another explicit parent requires a new reservation. A valid receipt followed by lease loss revokes the capture, rather than mislabelling the known reservation as unknown.

## Process ownership and limits

The existing anonymous request IPC, minimal environment and bounded output transport are retained. `LocalTaskProcess::hold` enforces startup deadline, one complete readiness frame, subsequent silence and liveness. Cancellation terminates/reaps the helper through the existing bounded transport. Unexpected exit, including exit zero, is lease loss rather than successful completion.

- Windows: the caller creates the child suspended, assigns its private non-inherited KILL_ON_JOB_CLOSE job, then resumes it. The worker verifies job membership. That check relies on this caller's owned-job contract; it does not authenticate arbitrary external jobs or the numeric supplied PID.
- Linux: the worker checks its expected PPID, installs `PR_SET_PDEATHSIG(SIGKILL)` and checks PPID again before acquiring locks or writing. The creating owner thread lives for the whole request/hold. Parent/thread death kills the bound child. There is no PID-based durable identity or clock/PRNG fallback.

Lifetime and operation lock probe failures fail closed. The lock probe is not a lease renewal or a persistent ownership record. The random epoch, private namespace, derived keys and session identifiers are private correlation data. POSIX fence files use 0600 and directory fsync; Windows inherits the protected parent ACL. Checksums detect accidental corruption, not malicious rewriting.

These tests do not establish hard spawn/loader/kernel/D-state/disk-stall deadlines, global quarantine/reaping, power-loss durability, hostile filesystem safety, real telemetry accuracy or real-cadence retention duration. Linux parent-death binding in this slice applies to the collector holder and guarded-operation workers, not all unrelated helper kinds. Production helper installation/discovery/trust and ordinary-runtime binding still need qualification.

## Verification

The focused suites exercise separate concurrent holders, operation-lock contention, delayed stale writes, valid Prepared recovery across a new epoch/session, malformed and maximal envelopes, frozen policy/selection/budget/clock guards, missing/corrupt fence preservation, helper exit/extra output/no acknowledgement, memory-only stop and no late revival. Independent Python probes kill only their own synthetic parent, observe both holder and outstanding guard identities terminate, then verify a new parent can acquire a fresh epoch/session without changing energy facts. Artifact checks exclude every collector fault seam from ordinary objects/binaries.

Native settings tests use the same private-default original review, cancellation, exact retry, receipt and controlling-terminal SIGINT flow with a leased injected owner. They do not provide ordinary-runtime activation.

Executed evidence (2026-10-29):

- Windows/MSVC: **58/58** distinct focused CTest cases (the new two plus the existing 56-case regression), including legacy energy/credit recovery, tariff/market/decimal, native settings/supervisor, stop/donation and import/build contracts. Both new suites pass **five repetitions**. Direct collector checks: **371**; independent parent-death/corruption/artifact checks: **63**.
- Linux/WSL/GCC: collector **366**, independent lifecycle **62**, authorization **1481**, authorization reconstruction **124**, capture **1392**, recovery **359**, compaction **3674**, synthetic supervisor **231**. Own collector/helper/core code passes **ASan+UBSan 366/62**. Linux uses direct GCC with warnings as errors, not Linux CMake. Path-dependent renderer line checks and OS-specific process/permission assertions explain the platform count differences.
- New leased-owner native PTY **20**; previous recovery PTY **19**, authorization PTY **21**, settings and runtime-settings PTY pass. The new flow uses an independently reserved native-shaped fixture identity, no device discovery, and a real controlling terminal for SIGINT.
- Python discovery: **123 tests, seven explicit fixture-dependent skips**. Architecture contracts **4/4** and index/history privacy checks are separate from process evidence.

An initial cross-platform Prepared-takeover test failed because its synthetic new-parent time preceded the Prepared batch's observed time. The production clock guard correctly refused inspection; moving only that fixture timestamp forward made the original recovery scenario pass. No clock guard was weakened. No allocator-failure, genuine kernel/disk failure, installed-miner or physical GPU/game evidence is claimed.

```text
cmake --preset tests
cmake --build --preset tests --target test_energy_collector canary_energy_collector_worker energy_collector_worker_fixture canary_energy_authorization_worker energy_review_fixture test_pearl_cuda_cli_host
ctest --preset tests -R "^energy_collector" --output-on-failure
```

Next work: bounded ordinary-runtime authorization/helper binding and packaging, explicit selection-change/orphan workflow, finite retention/archival planning, monetary billing continuation and qualified today's income/forecast integration. This slice closes a cooperative lifetime/fencing gap; it does not complete P-08 or the broader roadmap.
