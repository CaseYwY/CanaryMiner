# Exact reference-choice margins (P-10f)

M-OPTIMIZER; R-OPT-001/003; T-OPTIMIZER; D-78.

`economicChoice` adds the arithmetic part of candidate-change hysteresis to an immutable [economic study](economic-study.md). It is disabled by default and produces a **reference-only** choice. It does not own a clock, retain confirmations, accept a worker receipt or issue a mining command. The current mode is an explicit caller assertion: Unknown, Paused or AppliedWait. Unknown is never silently interpreted as paused or as a zero-score incumbent.

## Exact rules

Every score is the study's estimated **USD net per real elapsed second**, including its declared prospective transition/test costs. There is no conversion from accepted shares, display-rounded money or a duty-cycle percentage. A caller declares three independent nonnegative exact thresholds:

- Minimum score to resume from known pause, in USD per second.
- Minimum absolute improvement to change an already positive incumbent, in USD per second.
- Minimum fractional improvement relative to that incumbent, in [0, 1]. This is a declared heuristic, not statistical confidence.

Threshold inputs have at most 38 coefficient digits and 18 fractional digits. Intermediate arithmetic retains the existing bounded 96-digit/48-scale exact-decimal representation. Comparisons cross-multiply rational numerators/denominators without division or display rounding. If an intermediate cannot fit, Capacity has no selected point; it does not turn an unavailable comparison into a Hold or Switch.

For a strictly positive best score `B` and a strictly positive current score `A`, switch only when all are true:

```text
B - A > 0
B - A >= minimum_absolute_improvement
B - A >= minimum_fractional_improvement * A
```

The absolute and fractional limits are both required; they are not added as another transition expense. Exact ties keep the incumbent even if the study's declared-order tie breaker names another point. A best incumbent remains Hold. Resume requires a strictly positive best score at least equal to the resume minimum. A profitable incumbent below that resume minimum is not paused solely because it would fail a new resume threshold.

An incumbent missing from the permitted study candidates produces PauseIneligible. A zero/negative incumbent produces PauseNonpositive immediately, even when a different point is positive; switching away from a losing point does not bypass the separate pause/resume state transition. Complete studies with no positive candidate likewise produce PauseNonpositive. A missing study or unknown current state remains Unavailable. No pause reason contains a fabricated chosen point or a zero-income claim.

## Ownership boundary

The immutable study remains synthetic-qualified. A supplied AppliedWait value is not proof of installation; the future single owner must derive it from the exact [WaitApplied receipt](worker-wait.md), bind that receipt to the complete profile/current context, and check [original-result eligibility](economic-study-use.md) on every use. Receipt/application times and current scoring assumptions cannot be replaced by a request time or a fresh display tick. Unknown application, source changes and expiry need owner-level revocation.

This arithmetic does not count elapsed confirmation time, independent observations, cooldowns, retries or switch budgets. Those controls must span the actual parent/worker lifecycle, including failed transitions and stop, rather than being reset by constructing a new arithmetic choice. Native settings, original-time decision-to-receipt binding, temporal hysteresis and ordinary actuation remain open. `referenceOnly` stays true and `statisticalConfidenceAvailable` stays false.

## Verification

The focused unit suite covers independent thresholds, exact equality/one-unit-above boundaries at 18 fractional digits, incumbent ties, zero versus missing, no eligible candidates, invalid current modes/parameters, positive alternatives to a nonpositive incumbent and fail-closed arithmetic capacity. A separate Python `Fraction` model derives work rates and GPU-board cost rates independently, then checks state, point and wait for 384 varied synthetic cases, including all seven non-error outcomes.

Windows command after building the named test/probe targets:

```text
ctest --test-dir build/credits-check/windows -R "^(economic_choice|economic_choice_oracle|economic_evaluation_owner|economic_evaluation_controlled|economic_evaluation_artifact|economic_study_use|economic_study|economic_study_oracle|optimization_schedule|work_profile|architecture_contract|native_build_recipe)$" --output-on-failure
```

Linux directly compiles the new choice source and its unit/probe targets with `-std=c++17 -Isrc -O1 -Wall -Wextra -Werror -pthread`, reusing unchanged supporting objects and regression executables. The sanitizer variant adds `-g -fsanitize=address,undefined -fno-omit-frame-pointer` with instrumented supporting objects and leak detection, without suppressions. This is host/synthetic arithmetic evidence, not a Linux CMake/CUDA build or real economic qualification.

Final Windows focused CTest passes **12/12**. Windows/Linux ordinary and Linux ASan+UBSan/LSan each pass choice **45**, choice Fraction oracle **384 cases / 1152 comparisons**, owner **149**, controlled owner **215**, artifact isolation **3**, result use **189**, study **120**, schedule **67**, profile **1230** and the existing study Fraction oracle **192 cases / 2496 comparisons**. Initial 40-check runs passed; final review added exact-zero incumbent, all-excluded and arithmetic-capacity cases before the complete final reruns. Architecture/import checks pass.
