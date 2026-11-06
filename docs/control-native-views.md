# Control-thread current views and foreign caller denial (P-14i)

M-SUPERVISOR/M-PROFILES/M-OPTIMIZER; R-SAFE-001/002, R-PRO-001/002, R-OPT-001/002/003;
T-INTENT/T-PROFILES/T-OPTIMIZER; D-165.

Eight existing memory-only control owners now distinguish the current explicit parent's revocation from a stale task borrowing a healthy newer parent's objects: `WorkerControlIssuer`, `OptimizationSchedule`, `EconomicParentBudget`, `EconomicChoiceGuard`, `EconomicCommandOwner`, `WorkMeasurementOwner`, `WorkExperimentOwner` and `WorkExperimentBudget`. This is not native process ownership, new actuation, source qualification or ordinary activation.

## Denial is not a new permission

The borrowed `SessionIntent` already retains its explicit token and original native epoch. Its new `foreignCaller(token)` is a **denial-only** query: it detects a valid, still-current parent approached from a mismatched task scope. It cannot authorize any work and is false for a genuinely stopped/invalidated parent. All ordinary admission still uses `permits`. These single-control-thread owners need no new thread-local propagation or fresh epoch capture; queued/background ownership is separately fenced by the existing native lifetime primitives.

Stale reads return preallocated Stopped/empty views; issuer's by-value read returns an empty Stopped envelope. Poll/issue/append/check/acknowledgement/retirement and one-use handoff paths refuse before changing healthy clocks, tickets, reservations, studies, receipts or underlying channels. Refused **first starts** cannot cascade cancellation into a borrowed issuer that the new facade never acquired. The rejected instance remains single-use. Explicit stop/transport-failure cleanup remains available; a scope is not a security sandbox against malicious callers.

## Current state and historical ownership

Current schedule, budget, guard, command, capture and experiment reads check actual parent revocation without a timed poll. They withdraw current choices/sample requests/handoff deadlines, request cancellation where owned and expose `revokeRequired` for the enclosing transport. They do not sample clocks, invoke external callbacks, fetch, issue commands, join, perform native calls, or allocate replacement views. The schedule uses logically mutable cancellation state rather than casting away constness. Reading never refreshes evidence or budgets.

Revocation preserves original immutable studies/runs/measurements as historical records where already retained, and preserves every actual pending command/ticket and reservation/attachment. A matching late receipt can still settle its original pending command as Terminal, not restore authority. External Unreaped remains recorded after stop. Budget reservations are never refunded, and revocation/retirement is never proof of OS reaping or permission to restart. Transferred experiment evidence remains historical; transfer cannot be repeated.

## Reproduction and checks

Before the repair, the actual CPU/issuer/gate fixture reproduces **19 of 24** failing scenarios in both ordinary and instrumented builds. The separate issuer's native/intent stop paths and three harmless refused-start controls already pass. Cached current getters fail after both native fault and explicit stop; foreign calls consume/cancel healthy ownership; three foreign first-start cases cancel a borrowed issuer before acquiring it. Original pending receipts, studies and nonrefundable reservations are directly observed, not inferred from source strings.

`test_control_native_views` uses real immutable CPU studies, the actual in-memory issuer/gate and synthetic original work/power facts. It executes no worker/helper process, device, discovery, network, wallet, account, public source or installed miner. Twenty-four original cases plus five C++ `new`-refusal getter cases are registered as `control_native_*`. The allocation replacement is a separately linked fixture-only TU; issuer's historical by-value copies are intentionally not called under the zero-allocation contract. Rejected `new`/zero attempts do not qualify arbitrary allocators or physical memory pressure. Ordinary native artifacts must continue excluding unrelated calculation seams.

Final cross-platform, ordinary/sanitizer, full economic/native consumer, repetition, privacy and cleanup results are recorded in the execution ledger only after completion. Enclosing game/runtime/current views, all real source/safety/measurement bindings, hard native bounds, export, deployment, physical and release qualification remain open.

The final 29-case fixture passes ordinary/instrumented modes against fresh **38-unit** production archives, plus five further repetitions each, including far-future foreign clocks followed by healthy earlier calls. Fourteen control shipping/test hashes stay unchanged. Windows default-off full CTest passes **385/385**, with **51** affected cases repeated five times. Eight raw-work/worker, twelve native UI/control and seventeen economic/measurement suites also freshly rebuild/pass both Linux modes; exact counts, original intent/scope controls and privacy/cleanup are in the ledger. These tests do not convert synthetic data into actual source, mining, game-experience or release qualification.
