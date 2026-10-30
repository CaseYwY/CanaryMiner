# Receipt-bound temporal reference guard (P-10g)

M-OPTIMIZER/M-SUPERVISOR; R-OPT-001/003 and R-SAFE-001/002; T-OPTIMIZER/T-INTENT; D-79.

`EconomicChoiceGuard` joins the actual [evaluation owner](economic-evaluation-owner.md), [exact choice margins](economic-choice.md) and [worker issuer receipts](worker-wait.md) for one frozen worker incarnation. It retains temporal confirmation, original-application cooldown and a bounded rolling transition budget. It is disabled by default and every output remains **referenceOnly**. It sends no command and owns neither a worker process nor its cancellation/reaping.

## Parent and worker lifetime

All methods and both borrowed owners run on the same control thread. Both must borrow the **identical SessionIntent object and explicit token**, checked by `boundTo`; two independent parents with equal numeric epochs do not match. The enclosing lifetime updates the evaluator's current context/conditions before checking the guard. The frozen complete work plan, private worker binding, slot and incarnation cannot be rebound.

Start requires the exact binding's active, settled, **revision-1 Paused** receipt. Reconstructing a guard at a later Pause cannot reset its budget. Every later pending or settled command must have the next contiguous revision; skipping an intermediate command faults instead of guessing the applied history. Duplicate receipts retain their original timestamp and complete command. Out-of-plan explicit waits fault. A settled legacy Run has unknown application and supplies a Pause reference, never an assumed zero wait.

An outstanding command yields no new action. A same-mode Pause renewal or same-wait Run renewal preserves confirmation; a mode-changing request resets it. After a changed mode is actually observed, a new study must have an original evaluation time **strictly later** than its original receipt time. Same-millisecond evaluations cannot establish ordering. The initial revision-1 Pause permits the first study at the same timestamp after the already-proven boundary.

## Temporal confirmation and validity

The frozen policy separates the exact three margins from confirmation duration, minimum evaluations, maximum confirmation gap, application cooldown, transition window and maximum transitions. Confirmation duration is 0–1 hour, sample count 1–32, gap greater than zero and at most one day, cooldown 0–1 day, transition window 1 second–1 day and transition count 1–256. Result-age bounds retain the existing positive, at-most-one-day contract. Zero duration or one required evaluation is an explicit setting, not a hidden default.

Only a newly consumed evaluation with a strictly increasing **original evaluation timestamp** can advance confirmation. Re-reading a cached study, receiving its result late or changing a heartbeat revision cannot add samples or elapsed confirmation time. A changed target, context, mode, non-switching choice or excessive gap resets the sequence. A result already older than the confirmation gap on its first consumption cannot start a sequence. These repeated evaluations are temporal heuristic evidence, not independent market samples or a statistical confidence bound.

Paused mode remains a Pause reference while confirming, cooling down or budget-limited. A positive eligible incumbent remains Hold during those same constraints. Exact nonpositive/excluded incumbent results request Pause immediately through the existing arithmetic rules. Missing or expired evidence cannot substitute for a profitable Hold. Returned point/wait values always refer to the frozen plan.

Each immutable study has a separate `EconomicStudyUse` gate. Its earliest original dependency ceiling intersects the evaluator's admitted ceiling and the current worker lease. A ready Resume/Switch additionally expires when its last confirmation exceeds the maximum gap (the gap endpoint itself is inclusive; the returned deadline is exclusive). Changes in UTC/monotonic skew can only shrink a study's retained ceiling. A heartbeat can renew its worker lease, not the source or confirmation timestamps.

During a safe same-context CPU replacement, the evaluator withdraws its current study publication but exposes only the previous result sequence and its already-clipped ceiling. The guard may continue its **own previously admitted immutable study** only when those metadata match exactly, no cancellation is recorded, and its own validity gate still passes. The replacement metadata never grants a study to a caller that did not already hold it. Cancellation, context/condition revocation, actual completion and stop clear the metadata. A missed intermediate replacement cannot silently retain an older study. Earlier source-ceiling clipping is preserved even when it happened before the guard observed the new pending job.

Unsafe thermal/telemetry/game/drift conditions clear confirmation and block both the published result and the exact evaluation ticket outstanding at that event. A later success from that old ticket cannot restore the reference. Fault, plan/context rollback or mismatch, clock failure, issuer terminal state and explicit stop latch. A safety event during a pending worker command still yields no second command; its reason is Unsafe. The enclosing safety owner must use stop/channel revocation immediately rather than wait for a free command slot. `requestStop` here only withdraws references; it is not worker shutdown.

## Original application cooldown and budget

Cooldown begins at the first original WaitApplied time of a **changed applied mode**, not command issue, Accepted or receipt consumption. Each known Pause/Unknown-to-AppliedWait transition counts, including a resume at the same numeric wait used before pausing. AppliedWait-to-a-different-wait also counts. Same-wait lease renewals and Pause receipts do not count or restart cooldown.

The rolling budget is checked at each new receipt's **original application time** before that event is admitted. At most the configured number of original transition times are retained. Expired entries may disappear from the current displayed count, but remain available until the next application is checked, so a delayed receipt cannot hide a violation by arriving after older events aged out. An actual over-budget application latches Faulted. The budget at a future decision uses the current-time count; the exact window endpoint releases an old transition.

This is a single-incarnation reference budget. It does not reserve a transition, validate a command against a previously issued economic decision or preserve the budget across replacement workers. A future parent-owned execution coordinator must retain those budgets, bind the complete decision/profile/context to every original command/receipt, manage revocation and process exit, and prevent overlapping owners. Ordinary activation remains absent.

## Verification and scope

Tests use the real CPU evaluator, exact study arithmetic and actual in-memory issuer/gate. A separately compiled barrier fixture holds real calculations, including cancellation-ignoring completion, to verify no stale reference survives timeout, condition/context revocation or stop. Ordinary artifacts exclude the blocking hook.

The suite covers cached reads and gaps; original-time confirmation/expiry; context and target reset; same-mode renewals; Accepted/pending/application distinction; original-time cooldown; delayed receipt budget violations; same-wait resumes; legacy unknown application; skipped revisions; frozen-plan and exact-parent mismatch; post-application timestamp ambiguity; retained-result ceiling transfer; unsafe in-flight result blocking; and stopped-session nonrevival.

Windows builds the guard/controlled-guard, choice/probe, owner/controlled-owner, study/use/probe, schedule/profile and worker-control targets, then runs:

```text
ctest --test-dir build/credits-check/windows -R "^(economic_choice_guard|economic_choice_guard_controlled|economic_choice|economic_choice_oracle|economic_evaluation_owner|economic_evaluation_controlled|economic_evaluation_artifact|economic_study_use|economic_study|economic_study_oracle|optimization_schedule|work_profile|worker_control|architecture_contract|native_build_recipe)$" --output-on-failure
```

Linux directly rebuilds the guard, evaluator and worker-control objects plus the guard/owner ordinary and controlled fixtures and worker-control test with `-std=c++17 -Isrc -O1 -Wall -Wextra -Werror -pthread`. Supporting objects and unchanged arithmetic regression executables are reused. Sanitizer builds add `-g -fsanitize=address,undefined -fno-omit-frame-pointer`, instrumented dependencies and `detect_leaks=1`, without suppressions. This is CPU/synthetic reference evidence, not a Linux CMake/CUDA build, real provider/measurement qualification or physical GPU/game acceptance.

Final Windows focused CTest passes **15/15**. Windows/Linux ordinary and Linux ASan+UBSan/LSan each pass guard **439**, controlled guard **499**, artifact isolation **5**, choice **45**, owner/controlled-owner **149/215**, result use/study/schedule/profile **189/120/67/1230** and worker protocol **818**, plus both Fraction oracles (**384 cases / 1152 comparisons** and **192 / 2496**). Architecture/import checks pass. The initial 301-check guard passed on all three builds; four added regressions then reproduced delayed-budget, stale-first-confirmation, outstanding-unsafe-result and equal-time application defects. Those defects were fixed before the complete final runs; the added retained-result/cancellation and identity cases also pass.
