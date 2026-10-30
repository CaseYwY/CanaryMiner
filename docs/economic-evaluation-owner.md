# Owned economic computation and ticket admission (P-10d)

M-OPTIMIZER; R-OPT-001/002/003 and R-SAFE-001/002; T-OPTIMIZER/T-INTENT; D-76.

`EconomicEvaluationOwner` composes the [three-lane request schedule](optimization-schedule.md), one owned CPU calculation thread, immutable [economic studies](economic-study.md) and the [original-result validity gate](economic-study-use.md). It is disabled by default and starts at most once under an already explicit parent token. Linking it does not activate an ordinary supervisor, a provider request or a miner.

## Ownership and frozen requests

All public methods, the borrowed `SessionIntent` and its lifetime belong to one control thread. Only that thread reads the intent, advances the schedule, checks current conditions and admits results. The background thread receives a moved, owned input and an independent atomic cancellation flag; it never reads `SessionIntent`, calls a provider or touches a device. There is no runtime-injected computation callback in the ordinary build.

`start` freezes the outer enable switch, schedule controls and result-age policy. All three enable switches must be explicit. `poll` returns the schedule's original Market, Evaluation and RetestReview tickets; it does not execute a network request or physical retest. Separately permitted owners handle Market/RetestReview and call `completeExternal`. An evaluation without usable input may likewise be declined as Retryable/Rejected before submission. An external caller cannot mark an evaluation Complete, or settle an evaluation already owned by the CPU thread.

`submit` accepts only the exact currently pending Evaluation ticket, including generation, sequence, lane, attempt, original issue clocks and deadline. It requires the current context revision, complete matching work plan, matching safe conditions, the explicit parent epoch and evaluation clocks equal to the ticket's original issue time. Neither submission nor eventual completion restamps the input's source observations. At transfer, candidate and candle counts are bounded to 32 and 1000 per series; the existing builder remains responsible for all semantic, source and arithmetic validation. Its supported inputs remain explicitly synthetic.

There is one queued, running **or unconsumed completed** job. Submission cannot overwrite any of those phases. The job owns its input; external draft edits do not affect it. A short mutex protects only the mailbox, and calculation runs outside that mutex. The control thread retains the flight until it consumes actual completion, even if its deadline passed long ago. New work does not overlap an expired calculation. Input construction/allocation and full-plan fingerprinting still occur on the caller/control side; this is CPU-computation isolation, not a universal hard-latency guarantee.

## Current context and result admission

The caller supplies a nonzero monotonic context revision on every poll. **Any changed scoring input, source observation/configuration, reward/profile, tariff/opening or transition assumption must advance this revision**, including changes outside the work plan. The owner does not discover such changes by itself. Revisions may skip, but rollback or a changed plan under the same revision faults the lifetime. A newer revision immediately revokes the retained result and cancels the old flight; completion under the older revision cannot publish.

Unsafe/missing thermal conditions, unknown/active games and drift cancel an in-flight study. A bad condition followed by a good one cannot revive that cancelled flight or the old result. A declared fault latches the owner Faulted. A later independently scheduled evaluation can use restored non-fault conditions with fresh inputs. Changed immutable settings require a new explicitly owned coordinator, not mutation of the existing policy.

New submission first withdraws the previous reference. After computation actually returns, only the original owned job can settle its ticket. Publication requires all of: current parent/context, no observed cancellation, timely schedule acceptance, a successful study and current `EconomicStudyUse` eligibility. The use gate retains all original dependency deadlines; a result may finish within its operation deadline yet already be expired as a financial reference. NoPositiveCandidate remains a complete usable comparison with no chosen point; unavailable/unsupported/cancelled results never become zero income. Reading `snapshot()` alone advances neither clocks nor eligibility.

Source/current-configuration revision notifications are an explicit integration obligation. A caller failing to report a changed input is not certified by this interface. Native runtime source binding and complete safety qualification remain prerequisites for actuation.

## Cancellation, retry and stop

Operation expiry requests atomic cancellation but retains the slot until actual completion. Even a computation that ignores cancellation and returns a successful study is discarded after timeout, context change, bad conditions or stop. `completeExternal` cannot forge reaping of that owned calculation. Once its real completion settles the original ticket, the schedule applies its existing bounded retry/no-catch-up policy: at most three additional attempts, then the next regular cycle. RetestReview is never automatically retried as a physical experiment.

`requestStop` immediately withdraws publication, cancels the job and stops the schedule without joining. Pending remains visible until the computation returns. `stop` joins the owned CPU thread and settles only its own retained ticket. It preserves pending external lanes, because joining the CPU evaluator is not proof that another provider/helper has finished. The enclosing owner must cancel and settle those external tasks before dropping their ownership. Stop-before-start and a newer explicit parent token cannot revive an old instance.

Clock rollback, invalid UTC, cumulative UTC/monotonic disagreement over 2000 ms, revision/identity failure and external Unreaped halt this lifetime. User stop takes priority, while the schedule retains its Unreaped fact. Exceptions during computation produce a rejected Capacity result; admission exceptions revoke the owner. No thread is detached or forcibly terminated. A permanently stalled CPU/native allocator could still block the post-revocation join; hard process-exit and allocator-failure qualification are not claimed.

## Verification

Windows builds the two owner tests plus the existing study/use/schedule/profile/oracle targets, then runs:

```text
ctest --test-dir build/credits-check/windows -R "^(economic_evaluation_owner|economic_evaluation_controlled|economic_evaluation_artifact|economic_study_use|economic_study|economic_study_oracle|optimization_schedule|work_profile|architecture_contract|native_build_recipe)$" --output-on-failure
```

Linux directly compiles the owner, result gate, study and schedule with `-std=c++17 -Isrc -O1 -Wall -Wextra -Werror -pthread`, links the new tests against unchanged supporting objects and runs the unchanged regression executables. The sanitizer variant rebuilds the same sources/tests with `-g -fsanitize=address,undefined -fno-omit-frame-pointer`, instrumented supporting objects and `detect_leaks=1`, without suppressions. This is direct-GCC evidence, not a Linux CMake/CUDA build.

Final Windows focused CTest passes **10/10**. Windows/Linux ordinary and Linux ASan+UBSan/LSan each pass owner **149**, separately compiled controlled-owner **215**, and artifact isolation **3**, plus result-use **189**, study **120**, schedule **67**, profile **1230** and the independent Fraction oracle's **192 cases / 2496 exact comparisons**. Architecture/import checks pass. An initial test-only direct conversion of `optional` to the Boolean check helper failed compilation on both platforms; explicit `has_value()` fixes it. Initial runtime runs passed 131/178 checks; final additions exercise repeated-result replacement, skew clipping, independent external lanes and the complete four-attempt timeout budget.

Controlled tests use real threads with a barrier, not timing guesses: cancellation-ignoring late success, context/condition/stop revocation, pending-slot retention, owned settlement, frozen drafts, expired-on-arrival results and exception cleanup. Ordinary executable/library artifacts exclude the test hook; the controlled executable contains its intentional failure marker. No real provider, wallet, telemetry, GPU, game or mining work is part of these tests.

## Remaining integration

This owner supplies reference-result admission, not candidate-switch hysteresis/cooldown/budgets, an applied-setting receipt, worker actuation, native reviewed controls or real source qualification. P-09 measurement ownership and the current safety/source changes must be integrated before ordinary activation. Physical GPU/game, source/provider, release, hard-stop and long-run gates remain open.
