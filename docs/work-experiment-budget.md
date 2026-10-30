# Parent-retained experiment reservations (P-09d)

M-PROFILES/M-OPTIMIZER/M-SUPERVISOR; R-PRO-001/002/003, R-OPT-001/003 and R-SAFE-001/002; T-PROFILES/T-OPTIMIZER/T-INTENT; D-86.

`WorkExperimentBudget` is a default-off, single-control-thread budget for measurement attempts in one logical parent slot. It freezes its own policy, complete work plan and exact borrowed parent intent/token, and must outlive every attached [experiment owner](work-experiment-owner.md). Ordinary startup and `GameWorkerSession` do not yet construct it. The optional owner argument preserves isolated P-09c use; omitting it supplies no cross-attempt budget.

## Reserve before Run; never refund

Admission follows actual initial Paused plus baseline validation and precedes the first experimental Run. It reserves the **entire** plan's `maximumElapsedMs` and `maximumEnergy`, and consumes one attempt. Each immutable reservation retains its ordinal, original initial Pause, admission clocks and exact reserved amounts. Completion, transfer and retirement never refund a reservation, even when observed usage is smaller. A faulted or stopped admitted attempt retains its reservation and attachment; there is no automatic retry of a failed experiment.

Independent parent limits are:

- attempts: 1–256;
- minimum retest interval: 0–30 days, measured admission-to-admission on the monotonic clock;
- total reserved elapsed time: positive, at most 256 days;
- total reserved energy: an explicit positive exact amount, with at most 38 coefficient digits and 12 fractional places. Zero is not unlimited.

Attempts, time, energy and retest interval must all permit admission. Equality at a ceiling is accepted. Limit refusal adds no reservation and leaves the unattached budget Running; the refused experiment stops its own issuer. Attribution, clock, capacity or an admitted experiment's fault/stop latches the parent budget. There is no reset or second start. Native process replacement eligibility remains a separate, stricter gate even after an admission refusal.

The original parent dual-clock anchor survives retirement and replacement. Monotonic/UTC rollback or cumulative disagreement above 2000 ms refuses; constructing a new local experiment cannot reset that clock history. This budget schedules no work, performs no sampling, and does not directly close channels. The current experiment checks it on its control operations; the enclosing session must propagate cancellation and native revocation.

## Identity and one attachment

The first measured worker must be generation 1, and its initial command cannot predate budget startup. Subsequent measured generations must be **strictly increasing**, with unique private worker keys, the same parent key/slot/intent and full plan, and an initial command no earlier than the prior retirement. Generations need not be consecutive here: this budget accounts only for measurement attempts. The separate [economic parent budget](economic-parent-budget.md) still enforces consecutive economic incarnations. Neither budget claims to prove all native instances or cross-slot totals.

Only the actual experiment owner can privately admit, check, complete and transfer a reservation. A simultaneous attachment faults the budget; the existing experiment then observes that fault. Arbitrary caller-created records or a fresh budget cannot retire an existing attachment. All referenced objects share one control thread and must retain their documented lifetimes.

## Complete observations and transfer

Final Paused plus the complete final boundary records observed elapsed time and exact observed energy from the capture. These accumulate separately from reservations, exactly once per completed capture. They include captured warm-up/transition/drain intervals, but not unobserved tails, later Pause holding or subsequent economic operation. Failed partial captures do not become zero-consumption completed observations. Reserved energy is not a power reading, an actual-energy total or a physical hard cutoff between samples.

The one-use economic transfer marks the attachment transferred while retaining its issuer identity. The attachment remains occupied until explicit retirement; transferring or destroying the old experiment cannot make capacity reusable. A private `failedHandoff` path faults the reservation if economic admission fails after release. The old owner's public stop/transport callbacks remain inert after transfer, preserving exclusive successor authority.

Retirement requires a completed/transferred attempt, the **same** issuer bound to the original parent, Stopped state, no unresolved flight, and a settlement at least as recent as the transferred original. It releases the attachment without changing totals or history. The enclosing native owner must first prove actual reaping, absence of overlap and eligible exit/restart intent, and retire the economic owner under its own rules. The budget itself has no process handle or reaping evidence. In particular, the native test's orderly exit 0 proves cleanup and accounting only; production replacement remains restricted to eligible reaped exit 1 with complete settlement.

## Verification

Windows focused CMake/CTest passes **20/20**. Windows/Linux ordinary and Linux **ASan+UBSan with leak detection** each pass reservation **4,718**, experiment/handoff **1,087**, capture **25,203**, command **467**, guard **754**, profile **1,230**, protocol **818** and evidence-deadline **670** checks. The native experiment fixture passes **117/115/115** respectively; its counts include scheduler-dependent sample admissions.

The reservation suite performs 256 complete actual issuer/gate capture, economic handoff and retirement cycles, checks exact cumulative reserved versus observed totals and preserved first originals, and refuses attempt 257. It also covers independent time/energy/retest ceilings, no refund after failure, overlap/identity/plan refusal, incomplete/unsettled/live retirement refusal, parent stop, cross-attempt cumulative clock skew, failed successor admission and inert transferred-owner callbacks. Native CPU peers verify real reaping before retiring both budgets and retained reservations on withdrawal.

Windows additionally passes native economics/session/pending **221/126/67**, controlled command/guard **535/814**, evaluator/controlled **149/215**, shared-loop **60** and artifact/build/architecture guards. Linux directly rebuilds the changed budget/owners and affected tests with strict GCC C++17 warnings, reusing corresponding ordinary or instrumented support objects; this batch does not claim a Linux CMake run or a newly rebuilt Linux fleet suite. The preceding P-09c fleet evidence remains recorded separately.

Source observations and economics remain synthetic. Live completed-work/power/drift adaptation, native sample transport, fleet measurement admission/replacement, private persistence/review, ordinary settings/CLI and physical GPU/game acceptance remain open.
