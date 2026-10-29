# Bounded durable-energy producer and native recovery (P-08f)

This connects the [persistent energy owner](energy-recovery.md) to a bounded, generation-bound producer and an optional supervisor seam. A native recovery page is available under **Settings → Electricity → Energy history** and **Settings → Privacy → Energy history**.

**Development integration, not production activation.** The ordinary miner still supplies no `energyCaptureFactory`. Its existing in-memory energy toggle is not disk-retention consent. Opening either new page with no injected owner shows unavailable and opens no store. The later [P-08g authorization/enrollment helper](energy-authorization.md) supplies saved consent and system-random identities; whole-collector admission, ordinary-runtime binding, helper packaging and physical telemetry qualification remain necessary before ordinary-runtime activation.

R-ENG-001, R-PRIV-001/002, R-SAFE-001/002 and R-REL-001 remain partial; M-ENERGY/M-PRIVACY/M-SUPERVISOR; T-ENERGY/T-INTENT/T-PRIVACY/T-RELEASE; D-40. Monetary billing, today's income/net forecast and optimization remain separate roadmap work.

## Explicit private identity

`EnergyCapturePlan` requires independently true `persistenceAuthorized`, immutable binding, selected store mode/limits, a private namespace and a fresh parent session ID. Existing is the default. CreateNew remains explicit and uses the original helper protocol; no store/journal adoption, auto-repair or v1 migration is introduced. The owner always uses durable recovery.

`makeEnergyCaptureBinding` is a pure mapping function:

- Native inputs must be full `GPU-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx` UUIDs. Hex case is normalized; empty/duplicate IDs, ordinals, PCI addresses, names, unsupported origins and more than 64 devices reject. Synthetic fixtures use separate `fixture.` IDs.
- Domain-separated SHA-256 over a **caller-provisioned private 32-lowercase-hex namespace** and each canonical ID produces an opaque channel key. Sorted channel keys plus that namespace produce the selected-set scope. The explicit history lower bound and exact origin/adapter/basis remain part of the binding.
- Reordering the same devices changes only the transient ordinal-to-channel mapping. Adding/removing/replacing a device or changing namespace/lower-bound/basis cannot append to a differently bound store. No previous selected set is silently adopted.
- Raw UUIDs are not retained by the bridge or written into its ledger inputs. Derived keys still correlate local records and remain **private identifiers**, not anonymized public export data. Clearing a string is not secure memory erasure.

Native session IDs require `session.<32 lowercase hex>`; fixture IDs start with `fixture.`. The bridge does not mint entropy, persist a namespace or certify that a correctly shaped string is random. It is single-start, including after stop. An immediately reused committed session ID faults before collection; the ledger's full used-session set rejects older reused IDs too. Owner generation, batch sequence and device ordinals never supply persistent identity.

## Bounded producer and continuity

Startup submits only Inspect (or explicit empty CreateNew initialization). Initial inspection/recovery blocks new readings. Prepared input requires exact reviewed retry. An inspected Confirmed receipt can become Ready, but never grants the new parent authority to sample under the old session.

The first admitted reading anchors a fresh UTC/monotonic session after inspection. Inputs and ticks must be ordered on the current parent's monotonic domain. Wall/monotonic reversal, per-session skew beyond ±2000 ms or an out-of-range clock latches a fault, discards the volatile queue and revokes optional work. Duplicate per-channel timestamps reject with a continuity break.

The independent policy is frozen in the constructed plan:

- selection interval **500–60000 ms**, default 1000;
- maximum sample-and-hold gap from selection interval through **60000 ms**, default 5000;
- flush deadline **50–60000 ms**, default 2000;
- producer buffer **1–128 readings** and batch **1–buffer size**, both default 128.

There is one producer queue and one owner's isolated helper flight, alongside bounded immutable original/receipt snapshots. A batch is sent at the size target or oldest-reading flush deadline, only while the owner is Ready. IDs are `energy.<next committed sequence>` for both formats. Queue admission consumes no committed progress; only an accepted acknowledgement allows the next append. Permitted small negative wall skew waits until the mapped reading is no longer in the future before submitting it.

Each selected reading captures the continuity epoch **when it enters the queue**. Overflow, invalid/rejected input, unavailable startup/recovery, discarded queued observations and explicit worker-exit gaps advance that epoch. Earlier buffered observations retain their epoch; later observations cannot bridge the loss. Selection skips are reported separately. A missing sensor read breaks continuity **even between selected timestamps**. Covered zero stays zero; absent power never becomes zero. The ledger clips excessive intervals and does not extrapolate a shutdown tail.

Definite rejection, capacity exhaustion or an unknown acknowledgement faults capture and discards its later unsent queue. The original helper input/Prepared proof remains with the owner/journal. Collection never restarts automatically. Readings not reaching Prepare are not guaranteed across parent loss; volatile drop counters are not a complete persisted loss audit.

`tick`, `submit`, private snapshot access, inspect/retry admission and `requestStop` perform bounded memory work only. Storage stays in the helper. `requestStop` revokes admission and clears the producer tail; join occurs after miner-worker drain. No flush/receipt wait precedes stopping miners. Stop cannot undo COMMIT; actual kernel/D-state/physical-disk termination bounds remain unqualified.

## Supervisor and native review

An independently supplied factory is checked against `--no-energy` and parent intent, receives selected UUIDs before starting and stays frozen across child retries. The supervisor sends the same explicit valid/absent NVML milliwatt observation to the bridge, which owns its independent cadence. Child exits break continuity. Q/signal/automatic final exit revoke capture before optional join. No mining intensity, hardware-clock, fee, donation or financial-row policy is changed by capture/review.

The page shows capture/owner/recovery state, volatile buffer/drop/selection counts and separate committed/compacted sequence status. Scope/channel keys, original sessions/timestamps, every retained reading (including missing/zero) and original reset windows are hidden until **I** reveals them locally.

**F** inspects, **R** reviews, **Space** acknowledges the exact original, **Enter/F2** admits one revision-bound retry, **Esc** cancels/returns, and **Q/Ctrl+C** stops from every state. Owner generation/revision changes invalidate review; unrelated producer drop-counter updates do not. Queued status is distinct from confirmed-receipt status. One proven reviewed receipt is retained separately so later sampling jobs cannot erase that UI evidence.

There is no input edit, renewed timestamp, sequence edit, delete/rebuild or automatic retry. Opening/cancelling/retrying does not save a separate unsaved UI/electricity/tariff draft. The page uses the existing ASCII terminal renderer and bounded wrapped scrolling, without a browser/GUI runtime or new Unicode/RTL qualification.

## Verification and remaining activation

The suite covers authorization refusal, identity/selection mismatch and reordering, cross-parent monotonic reset, full 64-channel/128-reading input, exact integrals after overflow/gaps/null/zero/selection, flush deadlines, clock latching, original-input review/cancel/concurrent retry, compaction-boundary response loss and stalled-helper stop without late publication. Panel layouts cover 20/40/60/80/120 columns at 12/16/24/40 rows. Synthetic supervisor checks cover actual optional-factory feeding, UUID order, CLI disable, prior stop and no sampling after revoke. Linux controlling-terminal tests cover the ordinary unavailable entry and injected persistent owner, private default, cancel, one retry and SIGINT.

Executed evidence (2026-10-29):

- Windows/MSVC: the **54-test focused set passed** across the initial 51 completed cases and a final 4/4 targeted run (capture overlaps). The first build-plus-test wrapper reached its 120-second outer deadline before the last three cases; it was not a completed 54/54 CTest invocation. The remaining schema/build/document tests and latest capture code passed separately. Capture has **1392 checks**, also passing five consecutive repetitions.
- Linux/WSL/GCC: **1392 capture checks** and five normal runs, plus **1392 with ASan+UBSan** on own core/bridge/owner/helper code; compiler warnings are errors. Linux uses direct GCC rather than CMake evidence. The synthetic supervisor passes **231 checks**.
- Linux new recovery PTY **19 checks**; prior tariff **16**, manual-market **20**, session-energy **24**, settings and runtime-settings PTY suites pass. The long regression wrapper also hit its outer deadline after completed suites; remaining session-energy/repetition checks were completed separately. New review PTY was rerun after the final callback-publication adjustment.
- Python discovery **123 tests, seven explicit fixture-dependent skips**; final architecture contracts **4/4**. Windows regression includes old history/recovery/compaction, credit/cursor, exact arithmetic, native settings, donation, stop/intent and build/import contracts.

Initial validation fixed stale menu-field expectations and a missing tariff object in the direct Linux test link. Review also caught a refused retry callback needing to publish a newly latched fault; its regression now checks that path. A final capacity-rejection case verifies preservation of the committed prefix and discard of the later unsent producer tail. All readings, IDs, namespaces and subprocess failures are synthetic; these checks do not qualify real GPU sampling, physical durability or a production retention period.

```text
cmake --preset tests
cmake --build --preset tests --target test_energy_capture energy_review_fixture canary_energy_history_worker energy_recovery_worker_fixture test_pearl_cuda_cli_host
ctest --preset tests -R "^(energy_capture|energy_review_terminal)$" --output-on-failure
```

The later [P-08g](energy-authorization.md) provides helper-isolated consent/retention controls, private identity enrollment and fresh one-use plans through an injected native page. Next: lifetime-exclusive collection, ordinary-runtime binding and helper lifecycle/packaging qualification. Finite segment/session/window/database budgets still need real-cadence planning; this supplies no 30-day retention guarantee, raw archive, monetary bill state, actual-income source or production release authorization.
