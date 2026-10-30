# Parent-retained slot transition budgets (P-10h)

M-OPTIMIZER/M-SUPERVISOR; R-OPT-001/003 and R-SAFE-001/002; T-OPTIMIZER/T-INTENT; D-81.

`EconomicParentBudget` extends the [receipt-bound reference guard](economic-choice-guard.md) across replacement worker incarnations of **one logical slot**. The enclosing explicit parent retains it longer than every attached guard/issuer. It preserves original application cooldown, rolling transition history, incarnation capacity and the original dual-clock anchor. Outputs remain reference-only; this object neither reserves a command nor establishes process exit, authorizes replacement or sends work.

## Frozen parent ownership

The default-off policy independently declares cooldown, rolling window, transition count and maximum incarnations. Existing cooldown/window/count bounds apply; incarnation capacity is 1–256. Start is single-use under the exact borrowed `SessionIntent` object/token. The first guard freezes the private parent key, slot and complete work-plan fingerprint. The first worker must be generation 1; each replacement is exactly the next generation with a previously unused private worker key. All guards must use identical budget policy values and the same parent intent/token. A separate budget per logical slot preserves its own history; no cross-slot aggregate limit is implied.

Only one guard may be attached. Every attachment still requires the actual issuer's active revision-1 Paused receipt. A replacement's initial command cannot predate the previous guard's retirement. Reused identity, skipped/rolled-back generation, changed parent/slot/plan/policy, overlap or exhausted incarnation capacity latches refusal. Reconstructing a fresh budget at generation 2 cannot clear old history. Private keys/plan fingerprints stay internal, outside public stats.

## Exact retirement and historical receipts

The caller first stops the old issuer, then explicitly calls the old guard's `retire`. Retirement reads that exact issuer: it must be Stopped with no unresolved command and a matching settled receipt. Before detaching, the guard admits the last contiguous original receipt through the same accounting path as a normal check. A WaitApplied delivered immediately before retirement still counts; Accepted, a withheld receipt or a skipped intermediate revision cannot disappear during replacement. Legacy Run without an explicit applied wait is unknown history and faults the shared budget.

Successful retirement ends that guard permanently but keeps the parent budget Running. Destroying an attached guard without retirement latches the parent budget rather than silently releasing the slot. Explicit parent/guard stop, clock faults and other terminal failures prevent replacement. Destruction after intentional stop preserves Stopped. The parent budget must outlive its guards; retired guards no longer mutate it.

Retirement is **reference bookkeeping**, not proof that a worker process has exited or that GPU work drained. [P-10i](economic-command-owner.md) uses this budget through a decision-bound issuance owner. [P-10j](native-economic-session.md) retains it above actual native process owners and requires observed reaping/eligible exit plus settled control before retirement/replacement. The enclosing supervisor still enforces retry policy. A stopped issuer alone supplies none of those facts.

## Original-time continuity

The new guard starts paused and resets candidate confirmation, while inheriting the parent's original cooldown and current rolling count. A new worker's initial Pause does not reset either limit. Resuming the same wait in the replacement counts as a new transition. Same-wait Run renewals and Pause renewals do not count. Only each actual changed-mode WaitApplied advances the retained transition history.

A replacement requires a study originally evaluated **strictly after** its own initial Pause receipt. A prior worker's study, including one evaluated in the same millisecond as that receipt, cannot establish the required ordering. Initial-parent startup retains P-10g's original same-time first-study rule; replacement is a new mode boundary.

As in P-10g, history is pruned against the **next original application time**, never a display tick or delayed receipt-consumption time. A late receipt can therefore reveal an over-budget application even after an old event left the current displayed window; that faults the parent budget. At the exact rolling-window endpoint, the old event stops occupying the current budget. All worker lifetimes share the original parent's UTC/monotonic disagreement check, so replacing guards cannot reset cumulative clock skew.

## Verification scope

The existing ordinary and controlled-CPU guard suites now compose the real evaluator, issuer/gate and parent budget through multiple worker identities. They cover cooldown/window preservation, same-wait replacement, final-receipt retirement accounting, delayed historical violations, renewal non-counting, pending/Accepted refusal, unknown legacy application, overlapping guard/initial-command time, exact parent/plan/policy/identity binding, incarnation exhaustion, lost-guard refusal and stop/clock nonrevival. These are synthetic in-memory protocol checks; process reaping, command reservation/decision binding, production sources and physical GPU/game acceptance remain separate.

Initial ordinary/controlled runs passed 737/797 checks on Windows/Linux and Linux ASan+UBSan/LSan. A further same-millisecond replacement test then reproduced stale evaluation reuse (754/814 checks, one failure each). Marking replacement startup as a strict post-mode boundary fixes that case. The earlier mismatched-plan fixture was corrected to change a still-valid gap rather than exceed the plan's maximum; production plan validation was not weakened.

Commands and cross-platform build method remain those in [P-10g verification](economic-choice-guard.md#verification-and-scope). Final Windows focused CTest passes **15/15**. Windows/Linux ordinary and Linux ASan+UBSan/LSan each pass guard/controlled **754/814**, artifact **5**, choice **45**, owner/controlled **149/215**, result use/study/schedule/profile **189/120/67/1230**, protocol **818** and both Fraction oracles **384/1152** and **192/2496**. Architecture/import guards pass. Linux remains direct-GCC CPU evidence, not a production CUDA or physical GPU/game qualification.
