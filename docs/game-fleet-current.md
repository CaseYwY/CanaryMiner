# Enclosing game/worker current views and caller ownership (P-14k)

M-SUPERVISOR/M-GAME; R-SAFE-001/002, R-GAME-007; T-INTENT/T-GAME; D-167.

The optional `GameWorkerSession` now observes loss of its original explicit intent/native lifetime at current reads and action boundaries, not only at the next timed tick. Foreign callers receive empty/Stopped views and cannot consume the healthy controller's first start, source context, sample inbox, external tickets, clocks or force state. Ordinary startup remains unchanged and installs no fleet.

## Revocation versus ownership

Current root state, slot/game/force and economic/experiment reads request existing child cancellation without invoking a clock/source callback or joining. The actual parent's loss stops every owned issuer, command, experiment, evaluator and budget, and drops the unconsumed sample inbox. Pending launches are cancelled with their requested generation intact. They are done but **not reaped**: no process was created, and predecessor exit proof must not be borrowed.

Already-created native peers retain their actual background drain/reap responsibility. A stopped external market ticket can still accept its matching late settlement; an Unreaped outcome remains observable and cannot renew authority or be hidden by a worker's separately proven exit. Copied historical snapshots and original receipts are not rewritten. No cancellation fabricates command settlement, refunds an experiment reservation or grants replacement admission.

Denial precedes caller-supplied timestamps, context changes, input consumption and first-start bookkeeping. Explicit owner cleanup remains available; it is not a normal stale control action. Current snapshot return values may copy private originals, and existing runtime detector cancellation may attempt a caught projection allocation. This is not a blanket zero-allocation, lock-free or hard native/allocator latency guarantee.

## Reproduction and checks

The initial five queued-worker scenarios reproduce native/intent current-state leaks, foreign cancellation and failure to engage stop projection under allocation refusal; explicit owner-stop is the positive control. Both ordinary and ASan/UBSan builds show four failing scenarios before the repair. A later first-start stale-caller regression fails four of seven checks before its separate early-denial fix.

`test_game_fleet_current` retains eleven scenarios. Six use only an owned CPU entropy/identity helper and a queued never-launched worker. Five launch an owned held CPU peer over the actual worker channel; it proves initial Pause, refuses any Run and calls no work/device/network API. An explicitly synthetic sample is queued but never evaluated into mining permission. Parent loss must cancel it without another tick, foreign calls must leave it intact, and only actual cleanup proves reaping. Separate late external-Unreaped evidence remains unresolved after the owned peer exits. Unknown modes refuse before construction.

The queued allocation-refusal case engages C++ `new` failure during first root revocation and requires no escaped exception; it does not claim arbitrary allocator or live-thread allocation qualification. Final fresh cross-platform/native-consumer/repetition/privacy evidence belongs in the execution ledger after those checks finish. Actual source/safety/power/game-experience, remaining latency/export/deployment, physical and release qualification remain open.

Final Windows default-off CTest passes **408/408**, with **30** selected game/fleet/economic/evidence cases repeated five times. All eleven final scenarios pass both Linux modes against fresh **38-unit** production archives, plus five further repetitions each; existing native session/economic/runtime **134/336/78** and seventeen economic/measurement suites pass. New and prior relevant source hashes remain unchanged. Full native/CLI/PTY, privacy and exact owned-child cleanup results are recorded in the execution ledger, not inferred as physical or source qualification.
