# Receipt-bound measurement capture (P-09b)

M-PROFILES; R-PRO-001/002/003, R-ENG-001 and R-SAFE-001/002; T-PROFILES/T-ENERGY/T-INTENT; D-84.

`WorkMeasurementOwner` turns a bounded original observation stream into candidate-major, repetition-minor warm-up/measurement windows against the **actual worker issuer**. It freezes an explicit [work-profile plan](work-profile.md), borrows the same parent intent/token as that issuer, and starts only from its exact revision-1 Paused receipt. All methods belong to one control thread. Plan/source qualification remains SyntheticFixture; no ordinary factory constructs it.

## Capture is separate from actuation and sources

The owner exposes the next requested host wait and sample due time. These are experiment requests, not commands, mining permission or a promise that a sample occurred. It performs no hardware access, source I/O, automatic launch or issuer issue/acknowledgement. An enclosing experiment controller must own authorization, current safety, exclusive command delivery/acknowledgement, source mapping and native channel revocation. It must not overlap economic and measurement command authorities on one issuer.

The owner reads the issuer's actual current settlement. Accepted cannot start a measurement window. Each new settled revision must be contiguous, exactly bound to the original worker, and either an expected WaitApplied or a permissible Paused receipt. Missing/skipped/legacy application and unexpected mode changes latch refusal. Same-wait heartbeat receipts preserve the existing warm-up/window. The current native lease must remain Active; capture never extends it.

[P-09c](work-experiment-owner.md) supplies an independent default-off command owner around this capture. Capture preserves the exact renewal already pending before a request transition, allowing only that original to settle across a candidate change/final drain. A new old-wait command issued afterward is refused even at the same timestamp. Latest original sample/request metadata and a non-extending source ceiling support bounded issuance.

## Original observation stream and automatic windows

Start admits a complete-boundary baseline as sequence 1, bound to the explicit parent generation, nonzero counter epoch and full private identity fingerprint. Subsequent observations have consecutive sequence numbers and retain original UTC/monotonic times, applied wait, power, cumulative completed user/donation TMAC and safety/game fields. Original sample deltas must strictly increase and agree, matching P-09a; no timestamp normalization is performed. Samples must be no older than the maximum gap and cannot predate the latest consumed receipt or claim an unapplied wait. These strict contracts do not certify a native sensor/counter adapter.

The first complete-boundary sample after the expected WaitApplied starts warm-up. The first complete boundary at/after the requested warm-up duration starts measurement; the first at/after the measurement duration ends that trial. Each allowed overshoot is at most the configured maximum gap. Intermediate observations remain in the raw record, including non-boundary ones, and no partial batch is apportioned to hit a timer. Zero warm-up uses the same original start sample for measurement. Repeated trials at the same wait reuse only their shared boundary; candidate changes wait for their actual new application and a subsequent boundary sample.

Completing the last window enters Draining and requests no further Run. An actually settled final Pause **plus a subsequent complete-boundary observation** is required before the record becomes Complete. A pending Pause or child stdout supplies no such proof. Baseline-to-first-application, warm-up, candidate transitions and observed final drain remain in the full record as overhead. Setup before the baseline and activity after the last captured sample are outside its coverage.

## Budgets, gaps and revocation

Every poll checks current safety/game/drift, parent intent, the original worker lease, cumulative control-clock disagreement, the fixed baseline-derived elapsed deadline and age of the last original sample. Missing data, unsafe/game conditions, counter resets/decreases, identity changes, clock reversal, out-of-order consumption or skipped observations fault rather than fabricating zero. Samples and original settled receipts each have an independent **8192-entry** cap.

Energy is integrated exactly by sample-and-hold from each original power observation to the next, preserving watt-milliseconds over 3,600,000,000 for kWh. All captured overhead contributes. The declared positive energy ceiling is checked before accepting the next sample; an over-budget interval cannot become a valid profile. Explicit observed zero power/work remains distinct from missing data. No unobserved tail is extrapolated. This is an observed-energy admission bound, not a physical meter prediction or a guaranteed hardware energy cutoff between samples. Elapsed expiry requires timely caller polls and cooperative native revocation, not a hard kernel-stop guarantee.

Refusal/stop clears experiment requests, stops further issuer authorization and sets `revokeRequired`; the enclosing native owner must promptly close its channel. Later good data or a new parent epoch cannot revive this owner. These budgets belong to one capture; an eventual experiment coordinator must separately retain parent-wide retest admission and ownership across attempts.

## Immutable original and profile audit

Completion freezes a `WorkMeasurementRecord` with exact private worker binding, original admission/completion clocks, every contiguous settled receipt including initial/final Pause, and the complete `WorkProfileInput` samples/windows. Receipt times are not replaced by sample or consumption times. Stop retains an already completed record as historical evidence, not ongoing eligibility. Private keys/observations are not added to public stats, logs or defaults.

The caller can hand that immutable input to the existing bounded profile audit on an appropriate computation owner. Capture does not label it as a built/qualified profile or run expensive profile construction in the control callback. P-09c supplies a one-use same-incarnation experiment-to-economic command handoff. Source sampling/transport, parent retest budgets and fleet admission, native controls, persistent private review, real counter/power/drift qualification and physical GPU/game acceptance remain follow-ups.

## Verification

The CPU fixture composes the real issuer/gate and synthetic original samples. It independently rebuilds the captured profile and checks exact total/measured/overhead time, energy, user/donation work, original receipts and window boundaries. It covers zero warm-up, delayed/non-boundary endpoints, same-wait renewals, candidate transitions, final-Pause admission, missing versus observed zero, invalid sources, current constraints, stop, clock/lease expiry, energy/time limits, wrong parent/worker binding and both full 8192-entry capacities.

Windows uses CMake/CTest. Linux directly compiles the new owner/test with GCC C++17 and strict warnings, reusing unchanged profile/protocol/exact-decimal objects; the sanitizer variant instruments new code and uses the corresponding instrumented objects with ASan+UBSan and leak detection, without suppressions. Final counts are in the execution ledger. No device, native sampling helper, real financial provider or installed miner is run.

Final Windows focused CTest passes **10/10**. Windows/Linux ordinary and Linux ASan+UBSan/LSan each pass capture **25,125**, profile **1,230** and protocol **818** checks; the capture count includes actual protocol operations through both full-capacity loops. Windows additionally passes native economic **221**, command/controlled **467/535**, guard/controlled **754/814**, native build recipe and architecture. Architecture/import and exact-index/history privacy checks pass.
