# Bounded experiment command ownership (P-09c)

M-PROFILES/M-OPTIMIZER/M-SUPERVISOR; R-PRO-001/002/003, R-OPT-001/003 and R-SAFE-001/002; T-PROFILES/T-OPTIMIZER/T-INTENT; D-85.

`WorkExperimentOwner` owns [measurement capture](work-measurement.md) and exclusively issues/acknowledges that experiment's real worker commands. It has an independent default-off policy, freezes the complete profile plan, borrows the exact parent intent/token and issuer, and admits only the actual revision-1 Paused receipt plus the original complete-boundary baseline. All methods belong to one control thread. Ordinary startup and `GameWorkerSession` do not yet construct this owner; source qualification remains SyntheticFixture.

## Original authority and limits

Each Run retains a private immutable `WorkExperimentRunBinding`: exact command, original selection clocks, shared frozen complete plan, original sample, sample sequence, request revision and the original source/measurement/safety-game ceilings. Explicit zero wait is still an installed setting. Accepted remains pending; only exact WaitApplied settles it. An external issue or acknowledgement is detected against both the original settlement and the exact flight. Poll returns each new command once; the enclosing transport must submit it or report failure.

Capture now exposes its latest original sample, a request revision and an exclusive source-validity ceiling. Re-reading a cached sample cannot extend that ceiling when UTC/monotonic disagreement shrinks. New observations retain their original times and consecutive sequence; request revision changes only when the requested wait changes or final drain begins, not on same-wait repetitions or heartbeats.

The command horizon is 100–5000 ms. Its independent renewal margin must be positive and at most half that horizon. Each Run's deadline is the minimum of the new horizon, original sample freshness, the baseline-derived fixed elapsed limit and the complete current safety/game permission. Both source and measurement ceilings retain the earliest dual-clock mapping. A still-pending Run is checked against its **own** sample original even after newer samples arrive; its whole original lease must remain covered. Renewal headroom cannot revoke a covered pending flight or change its deadline. Insufficient headroom does not issue another Run; timely observations and polling are still required before the actual lease expires.

Current stop, safety/game/drift loss, missing data, clock/source expiry, changed binding, failed delivery or budget/capacity failure latches revocation. The enclosing native owner must close its channel immediately. No second Pause overwrites an unresolved flight. A matching late original can settle historical bookkeeping but cannot revive the experiment, publish a complete record or grant a handoff. Run commands do not predict energy between samples; P-09b's observed-energy admission bound and physical-stop limitations still apply. Total command revision is capped at 8192; capture's independent sample and receipt caps remain.

## Exact in-flight transition handling

A complete-boundary sample can end a window while a same-wait renewal is still pending. Capture freezes that **exact pre-transition command**, observed before advancing the request. It permits only that original to settle after a candidate change or entry into Draining. The old wait cannot start the new candidate's window. Once it settles, the command owner issues the new requested wait or final Pause.

This exception is based on observed method ordering and complete command identity, not on `issuedMonoMs <= boundaryMonoMs`. A new old-wait command issued after the transition is rejected even when it has the same millisecond timestamp. Skipped settlements, new revisions and altered originals cannot borrow the exception. Two deterministic issuer/gate regressions reproduced rejection of legitimate pre-transition renewals before this fix; converse same-millisecond cases preserve refusal.

## Record completion and control completion

Final Paused plus a complete-boundary observation freezes a `WorkExperimentRecord` containing P-09b's full receipt/sample record and every immutable experimental Run binding. It can be independently audited with `buildWorkProfile`. Captured warm-up, transitions and final observed drain remain overhead; no missing tail is extrapolated.

The command owner then enters **Holding**, not released ownership. It may renew only Pause while awaiting transfer. The separate handoff timeout is 1–60000 ms, defaults to 5000 ms, and starts from original record completion. Cached checks and Pause renewals cannot extend it. Captured source freshness no longer authorizes work in Holding, and no further samples mutate the completed record. Current constraints, clocks, stop, channel state and the fixed handoff timeout still gate holding. Expiry revokes; destroying a live Running/Holding owner also revokes.

## One-use economic handoff

`EconomicCommandOwner::startAfterMeasurement` is the only release path. It consumes the actual live experiment owner rather than accepting a caller-provided record or arbitrary noninitial Pause. It requires the same issuer and parent object/token, unchanged full plan, Holding state within its original timeout, no pending command and the exact current settled Pause. Failure cannot restore a released experiment to Running.

Successful release permanently marks the experiment Transferred. Its subsequent poll, acknowledgement, append, stop, transport-failure callback and destruction cannot mutate the successor's issuer. The economic owner retains the immutable experiment record. If economic admission fails after release, it stops the issuer; there is no split authority or automatic rollback.

The economic guard's public `start` still requires revision 1. Its private measured admission verifies the original revision-1 Pause, the final capture and current Pause, full plan/worker identity and complete clocks. The existing economic parent-slot budget attaches using the **original initial command**, retaining sequential generation, unique-key, retirement-time, single-attachment and existing cooldown/history rules. Neither handoff nor constructing another owner creates a fresh parent budget or a reaping proof. Experiment trials are not economic transitions. [P-09d](work-experiment-budget.md) supplies an optional separate parent-slot experiment budget, reserving the whole attempt before Run and retaining its attachment through transfer until explicit stopped/settled retirement. Failed successor admission faults that budget; the old owner's public callbacks remain inert after transfer.

An independent handoff barrier requires an economic study whose original evaluation time is **strictly later** than the transfer, including same-millisecond ambiguity. It does not restamp an application receipt. The experiment admission clock anchor is retained by the successor; the parent budget keeps its own original anchor. A safe Pause renewal may occur before a qualifying study exists. The profile, reward and price qualification gates still apply to that study.

This implements the single-incarnation measurement-to-economic seam. P-09d adds optional cross-attempt reservations. Live completed-work/power/drift adapters, native sample transport, fleet admission/replacement integration, private review/persistence, ordinary settings/CLI and physical GPU/game qualification remain open.

## Verification scope

`test_work_experiment_owner` composes actual capture, issuer/gate, profile audit and CPU evaluator. It checks original source/deadline binding; pre-transition renewals through candidate change/final drain; full immutable records; current constraint/permission withdrawal; Accepted/late historical receipt distinctions; invalid/bypassed authority; fixed elapsed and holding limits; one-use/wrong-plan/wrong-issuer handoff; unchanged ordinary noninitial-Pause refusal; post-transfer study ordering; cumulative clocks; and parent-budget attachment/retirement.

`test_work_experiment_native` uses self-owned CPU peers, actual `WorkerControlClient` and `WorkerProcessOwner` pipes. It runs a complete synthetic observation experiment, final Pause, one-use transfer and an economic boundary application, then verifies channel revocation and real reaping. Separate withheld-application cases cover thermal withdrawal, parent stop and delivery failure. Work/power observations are explicitly generated by the parent fixture; this is **not** native sampling transport or real work/power qualification. Native polling counts vary with scheduler timing.

The first integration fixture incorrectly required no command while the handoff study was too old and assumed a fixed half-lease Pause renewal point. Existing economics correctly permits safe Pause renewal and uses its no-new-decision headroom. The fixture now checks the meaningful condition—no Run from the old study—and consumes actual returned Pause commands. Production economic timing was not weakened.

Windows uses focused CMake/CTest; Linux directly rebuilds changed owners, dependent native sessions and tests with strict GCC C++17 warnings. ASan+UBSan uses the corresponding instrumented support objects, frame pointers and leak detection without suppressions. Final results are recorded in the execution ledger. These CPU/native-channel checks do not qualify Linux CMake, production CUDA, physical deadlines, real sources or the installed miner.

Final evidence: Windows **19/19**. Windows/Linux ordinary and Linux ASan+UBSan/LSan pass capture **25,203**, experiment/handoff **1,087**, command **467**, guard **754**, native economics **221**, native session **126**, pending withdrawal **67**, profile **1,230**, protocol **818** and evidence-deadline **670** checks. The native experiment fixture passes **103/101/101** checks respectively; counts include scheduler-dependent sample admissions. Windows additionally passes controlled command/guard **535/814**, evaluator/controlled **149/215**, shared-loop **60**, artifact/build/architecture guards. Final architecture/import and exact-index/history privacy checks pass.
