# Session-bound economic result validity (P-10c)

M-OPTIMIZER/M-PROFILES; R-OPT-001/003, R-SAFE-001/002; T-OPTIMIZER/T-PROFILES; D-75.

`EconomicStudyUse` is a memory-only, default-off lifetime gate over one immutable [economic study](economic-study.md), under an existing explicit `SessionIntent` token. It retains the original study and a copied result-age policy; callers keep one gate for that result's entire lifetime. It does not rebuild a study, replace inputs, rescore candidates or execute a schedule ticket.

**Eligible means that this original synthetic comparison is still usable as a bounded reference. It does not authorize mining, switching, measurements or networking.** The underlying study still accepts only the explicit synthetic source/work/tariff contracts. A later runtime owner must separately bind current source/configuration revisions and qualified safety before actuation.

## Complete-result binding

The study generation must equal the current explicit parent epoch. Each check receives the current full work plan and conditions; the complete plan fingerprint must still match the immutable study. Identity, driver/environment, measurement settings, wait points, freshness and budget changes revoke the old result rather than treating it as a reusable model-name estimate.

The original study retains its actual-credit contract, measured-record-bound reward model, economic policy, tariff/opening, prices/FX and every candidate's transition/test assumptions. External drafts cannot mutate that record. The future owning runtime must freeze this configuration or explicitly revoke its gate on a change; this class is not a source-change detector or an asynchronous result-acceptance protocol.

## Earliest original expiry

The copied result-age limit defaults to 120000 ms and accepts 1 ms through one day. It is independent of the sampling, market-refresh, evaluation and retest-request clocks. An effective exclusive deadline intersects:

- original evaluation UTC/monotonic time plus the configured result age;
- each PRL/USDT and USDT/USD observation's original UTC and monotonic receipt plus the study's market-age limit;
- the measured profile's original end plus its validity;
- the reward model's original validity endpoint;
- the original scenario's end;
- every **permitted** candidate's transition/test-assumption validity, including losing candidates;
- the independently configured tariff-currency FX validity when applicable.

Excluded candidates do not constrain the comparison's validity. Missing data never becomes a zero-income result. Zero or negative complete candidate scores, or an explicitly empty permitted set, produce `NoPositiveCandidate` with no selected point/wait; that observation itself still expires.

Original UTC-only source deadlines are bound to the evaluation's monotonic clock, not first consumption time. Subsequent checks may shorten the published ceiling but cannot extend it as relative clock skew decreases. Equality is expired. A delayed first check cannot restart an observation's age, and reading `snapshot()` does not refresh it. Backward/future clocks, arithmetic overflow or cumulative UTC/monotonic divergence greater than 2000 ms fault the gate.

## Revocation and ownership boundary

Missing/unsafe thermal telemetry, fault, unknown/active game, measured drift, changed plan, expiry or clock/attribution failure latches this result unusable. Later good conditions cannot revive it. Explicit stop wins even after another failure, and a newer explicit token cannot revive the old gate. Failures clear the candidate, wait and validity deadline while retaining the original evaluation metadata. Exceptions during validation likewise revoke the gate.

An eligible snapshot exposes only the study's original best measured point and its complete-batch wait setting; the exact score remains in the immutable study. It does not mark a requested or actually applied worker setting. Live result-ticket admission, selection hysteresis/cooldown/switch budgets, actual transition accounting, worker acknowledgements, native controls and qualified financial/safety sources remain separate work.

## Verification

Focused tests construct genuine immutable studies using the existing synthetic fixture builder. They independently tighten each expiry source, check losing/excluded candidates, exact expiry, late first use, skew reduction, plan/condition revocation, terminal stop, token changes, monotonic overflow and nonpositive/empty distinctions. Existing exact-study/Fraction-oracle, work-profile and independent-request-clock tests remain regressions.

Windows builds `test_economic_study_use`, `test_economic_study`, `economic_study_probe`, `test_optimization_schedule` and `test_work_profile`, then runs:

```text
ctest --test-dir build/credits-check/windows -R "^(economic_study_use|economic_study|economic_study_oracle|optimization_schedule|work_profile|architecture_contract|native_build_recipe)$" --output-on-failure
```

Linux uses direct GCC with `-std=c++17 -Isrc -O1 -Wall -Wextra -Werror -pthread`, rebuilding the new implementation/tests and request-clock test while reusing unchanged study/profile/forecast/market/credit/arithmetic/time/transport objects. The instrumented variant uses `-g -fsanitize=address,undefined -fno-omit-frame-pointer`, unchanged instrumented dependencies and `detect_leaks=1`, without suppressions.

Final Windows focused CTest passes **7/7**. Windows/Linux ordinary and Linux ASan+UBSan/LSan each pass result-use **189**, exact-study **120**, request-clock **67**, work-profile **1230**, and the independent Fraction oracle's **192 cases / 2496 exact comparisons**. Architecture/import guards pass. The initial test initializer's missing brace was corrected; the first runnable gate suite reported 14 failures: 12 late-first-use deadline assertions and two invalid aged-market fixture constructions. Binding UTC-only deadlines at the evaluation's original monotonic time fixes the 12 assertions. The two fixtures now preserve completed-candle timing and declare a matching fresh opening instead of inventing unsupported accrued state. Complete final runs pass, with no weakening of source/arithmetic/clock validation.

These checks do not start a GPU, miner, game, provider request or actual-income source. Allocation-failure injection, native scheduler/actuator timing, live configuration/source notification and physical qualification remain untested.
