# Native local credit-recovery review (P-07d)

Follow-up [P-07e](credit-cursor.md) adds committed-checkpoint status and full original BEFORE/AFTER review for opt-in fixture cursor streams. Tokens default hidden and are revealed locally with the other private fields. Exact reviewed retry may atomically commit facts plus cursor; there is no manual cursor edit or production collector activation. The original P-07d behavior/evidence below remains applicable to non-cursor streams.

This slice adds **S → Privacy → Credit recovery / collection status** to the existing native terminal settings. It introduces no GUI/browser, production collector, source registration, file picker, account manifest, importer/exporter, source cursor or income activation. With the ordinary miner or idle settings the page truthfully says **UNAVAILABLE: no qualified local credit collector/owner is active** and does not open, create or replay a credit store. `FinancialView.enabled` remains false.

Requirements: R-UI-007/008, R-INC-001/003–007, R-SAFE-001/002, R-PRIV-002; all remain partial where broader functionality is missing. This integrates the native review/controller with the [isolated owner](credit-owner.md) and [durable pending core](credit-recovery.md). Only the test-only native harness injects a real fixture-bound owner; production activation still needs a qualified source and reviewed binding, helper installation and collection lifecycle.

## What is reviewed

The panel consumes immutable **in-memory** owner snapshots, not SQLite, journal files, an HTTP API or a freshness-inventing render query. It distinguishes:

- owner state: Stopped, Starting, Ready, Busy, NeedsReconciliation or Faulted;
- persistent versus explicit volatile compatibility mode;
- recovery state: Empty, Prepared, Confirmed, Rejected, Diverged, Unavailable or Disabled, with the original core's meanings;
- data generation/snapshot revision, queue admission/completion/successful-operation tickets, pending jobs/reserved bytes/discards and typed transport/ledger/apply/proof status;
- retained durable entry identity versus uncertain original input. A durable sequence is **not necessarily a committed source cursor**, and even a successful query ticket is not an income acknowledgement;
- **unknown write outcome**, which takes precedence over any older confirmed receipt still visible in the snapshot.

Private provider, scope/generation, IDs, original instants, amounts, records and coverage are hidden initially. **I** deliberately reveals them locally, with a warning not to share screenshots or terminal capture. There is no path, credential, raw-wire, diagnostic dump, clipboard or export action. Revealed IDs are bounded and control bytes are neutralized; this local display does not anonymize older logs, API, stores, shell capture or external screen recording. Leaving/reopening the panel hides values again.

The original batch view includes provider/contract, worker versus explicitly acknowledged account scope, asset/amount/time/revision basis and declared precision/history start. It lists retained sequence/ID, unchanged source-as-of/observed UTC milliseconds, **every bounded original record** (revision, kind, reference, posted instant and replacement PRL amount) and coverage assertions `(fromExclusive, throughInclusive]`. Empty coverage is unknown, not zero. These are source declarations under the fixture contract, not actual holdings, sales, balances, payout records or mined-dollar profit.

`CreditOwnerSnapshot::uncertainBatch` is an aliased immutable reference to the original retained job. It survives a lost/invalid helper reply or cancellation even when `reply` is absent; it is not recreated from displayed totals or mutable UI fields. Old snapshots continue to retain their original immutable input after a later reconciliation. Holding that input does not mean it committed or that the queue is durable.

## Inspect, explicit retry and race protection

**F** queues one inspection if the injected owner is available and has no pending operation. It does not retry. A normal inspection retains the durable core's no-auto-apply/current-latest-ledger-proof rules; an inactive production page cannot inspect a path merely because settings was opened. Refresh is not accepted while reviewing a retry.

**R** opens retry review only for a persistent, fixture-qualified **NeedsReconciliation** snapshot with retained original uncertainty and no pending jobs. Stopped, busy, starting, ready, faulted, unavailable or volatile-compatibility states offer no replay shortcut. Rejected input is not treated as uncertain recovery. A faulted owner needs a separately reviewed future lifecycle, not a reset button on this panel.

To submit once:

1. R captures the exact immutable owner generation/revision and original input.
2. I reveals the local private values; scroll to inspect the original records and coverage.
3. Space explicitly acknowledges **exact original replay**, which may already have committed. The persistent footer shows `[ ]`/`[x]` without relying on color, including small terminals.
4. Enter/F2 makes **one** revision-bound admission request. Merely entering review, revealing input, acknowledging, cancelling, scrolling, refreshing a frame or pressing repeated buffered Enter does not replay a batch.

Esc cancels the review without writing; Esc again leaves the panel. Q/Ctrl+C still stops from every state. There is no editable amount, source/timestamp replacement, retry loop, restart, schema migration, delete/reset/rebuild, checksum edit, cursor advance or alternate writer to bypass uncertainty. UI/runtime/market/electricity drafts save independently and are never implicitly committed by recovery.

**The final owner check is atomic with admission.** `retryReviewed(generation, revision)` holds the owner mutex while comparing the exact current revision/state and admitting the retained original job. A generation change returns `StaleGeneration`; any revision change returns `ReviewChanged`. The panel also cancels stale review when re-observed, but the mutex check is required to close a race between the final observed frame and the user click. Callbacks are bound to one fixed owner object/lifetime; replacing a backend/binding requires closing/reopening review, not substituting an unrelated owner with coincidentally identical counters. Legacy internal `retryUncertain()` remains for compatibility/tests, not the native review action.

Queue admission is shown as **QUEUED, NOT a durable acknowledgement**. Only the matching generation/ticket's successful Ok/exact Replay with persisted Confirmed receipt, a Ready owner and no remaining uncertainty produces the panel's confirmation notice. A failure retains the original uncertainty and never erases it or fabricates zero. No panel action changes the existing helper's write ordering, stored original times, checksum, latest-ledger proof or SQLite schema.

## Boundaries, migration and rollback

The production settings has empty in-memory access callbacks; it does not construct/link/start a credit collector. The native mining CLI still links only its prior market/economics support, not a credit helper. The host settings targets need the existing exact-decimal formatter for the optional private record view, not a SQLite dependency. Injected callbacks may only return in-memory immutable snapshots or perform bounded queue admission; no provider/file/helper wait belongs in a key/render/safety callback.

The test-only `credit_review_fixture` executable is built only by the CPU test profile, not installed or added as a production command. It uses a fixed synthetic source contract, filename and invented credit. Its `--prepare`, `--settings` and `--facts` modes support deterministic PTY tests; they are **not** a user importer, account manifest or general-purpose recovery tool. The fixture explicitly owns a controlling terminal for SIGINT; rejected redirected settings does not activate an owner. The fake source never accesses a pool, GPU or installed miner.

No disk schema/preferences migration, new dependency, public network call, mining command or donation/proof change is introduced. Rollback leaves existing pending intents/receipts untouched. Do not roll back to a volatile writer, edit/delete a sidecar or rebuild a missing ledger to get around the original recovery restrictions.

Remaining: real source qualification/terms/attribution, persistent producer/source cursor, production collector binding/settings and helper packaging, native import/export with private-data review, fault/restart/source-change workflow, actual-income/forecast/net integration, genuine kernel/driver/disk/parent-death quarantine and full release/GPU/game qualification. Completing this review seam does not complete P-07 or the whole planning objective.

## Validation

Native tests cover private defaults, record/time/coverage review, acknowledgement, cancel/stop, duplicate Enter, stale review invalidation, all typed states/admission failures, no-color 60/80/120-column 12/24-row scroll/focus controls, unrelated dirty UI drafts, real helper Prepared inspection/no mutation, atomic stale-revision refusal, exact successful replay with unchanged observations and unavailable original-response recovery. The timeout/lost-reply fixture retains original amounts/times rather than attempting reconstruction. POSIX PTY exercises the actual parent settings/isolated helper, not a browser simulation.

The PTY also opens the ordinary native CLI against a directory containing a prepared fixture bundle: the unavailable production page does not discover/replay it. It keeps a separate unsaved UI draft and exits without writing preferences. An external facts probe waits for **pending jobs 0**, not merely an older Confirmed label, before trying to acquire the helper's operation lease. Read chunks can split a native frame, and an immediate helper can complete before a transient queued frame renders; neither is used as a fake timeout/retry signal.

Validation initially exposed a missing exact-decimal formatter link on the previously standalone host settings target; the affected host targets now explicitly link the existing market arithmetic library, without introducing credit storage into the mining CLI. The synthetic seeding harness also initially omitted its required query contract and correctly failed closed. Tests were corrected to wait for complete semantic readiness rather than partial PTY reads or an in-flight helper lease; no journal timeout, lease bypass or source validation was weakened to make them pass.

Executed evidence (2026-10-28):

- Windows/MSVC focused CTest **44/44** passes, retaining existing process/owner/journal/crash recovery, donation, intent/shutdown, history, settings, electricity/decimal-oracle and market/HTTPS regressions. The new native review program passes **200 checks**, also in five consecutive repetitions. Existing credit owner passes five repeated runs.
- Linux/WSL/GCC: **200 native review checks**, also five consecutive runs; **21 new controlling-terminal checks**, also five consecutive runs. Rebuilt credit process/owner/journal pass **65/62/133 checks**. The instrumented own helper/core/review/owner code passes **ASan+UBSan (200/62 checks)**; the external system SQLite runtime is uninstrumented. Direct GCC host builds do not claim Linux CMake or native GPU qualification.
- Rebuilt Linux settings/runtime/manual-price/session-energy PTY regressions pass, including the earlier **20 manual-reference** and **24 session-energy** checks; session energy/settings and its sanitizer pass **233 checks**. The new page never changes energy/default activation, mines, starts a source or saves another draft.
- Linux Python discovery passes **122 tests, seven explicit fixture-dependent skips**, not seven assumed native passes. Explicit operator-help/exact-profile tests pass **12+1**; schemas/build recipe/import/architecture pass **7/2/1/4**. Native Windows full Python discovery/toolchain/default-encoding issues, pinned verifier, real CUDA/SASS/protocol and full release gates are not newly qualified.
- Ordinary helper/library seam scans, source/index/history and approved-public-identity/DCO review are separate from tests. No binary, raw measurement, journal, configuration bundle, database or private path is contribution evidence; the local deny-list remains untracked. No dependency was installed/downloaded and no existing miner was replaced.

```text
cmake --preset tests
cmake --build --preset tests --target test_credit_review credit_review_fixture canary_credit_worker credit_worker_fixture
ctest --preset tests -R "^(credit_review|credit_review_terminal|credit_owner|credit_journal)$" --output-on-failure
```

Host and synthetic tests are not physical durability, hard OS deadline, real-pool or production binary qualification.
