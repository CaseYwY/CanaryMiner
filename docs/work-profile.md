# Bounded offline work/energy profiles (P-09a)

M-PROFILES; R-PRO-001/002/003, R-ENG-001, R-SAFE-001/002 and R-PRIV-002; T-PROFILES/T-ENERGY/T-INTENT/T-PRIVACY; D-59. `canary_work_profiles` validates complete caller-supplied measurement records and creates an immutable exact profile. It supplies no sampler, benchmarking controller, hardware access, storage, native settings or economic decision. Policy defaults disabled and only **SyntheticFixture** records are admitted in this slice.

## Identity and explicit plan

The local identity binds private device identity, platform, software/driver/kernel versions, work shape, thermal/environment context, sensor adapter/scope, power basis/origin and the fixed complete-batch host-wait mechanism. A versioned length-prefixed hash avoids delimiter ambiguity. It is an identity comparison, not authentication of a physical measurement. No GPU-model/ordinal inference or cross-machine profile adoption occurs.

The complete plan additionally binds its key, ordered candidate waits, telemetry cadence, maximum sample gap, warm-up duration, measurement duration, repetitions, total elapsed/energy budgets and freshness. Changed measurement settings invalidate an existing use just as changed hardware/software/environment identity does. A profile retains the original plan, samples and windows for later private review. No measured numbers are shipped as defaults.

- Up to **32 distinct 0–1000-ms host waits**, **32 repetitions each**, **8192 samples** and **1024 windows**.
- Telemetry target cadence and maximum acceptable gap are separate, each bounded to **60 seconds**; actual interval coverage is audited against the gap bound. Cadence is a requested setting, not proof of actual sampling frequency.
- Warm-up is independently configurable from **0–1 hour**; measurement duration is at least the requested cadence and at most **1 hour**. The explicit total elapsed budget is at most **24 hours** and must fit the requested minimum windows.
- A positive exact energy ceiling is required in the declared power basis. Freshness is independently configured up to **30 days**. These are implementation bounds, not recommended settings or hardware safety limits.

## Audit and arithmetic

Samples contain the same run generation, counter epoch and complete identity fingerprint, actual applied host wait, exact UTC/monotonic instants, raw integer milliwatts, cumulative completed **user TMAC** and **donation TMAC**, complete-work-boundary markers and explicit safety/game observations. Work counters are neither accepted-share counts nor income. The real completed-work adapter remains unqualified.

Every consecutive UTC and monotonic delta must agree and increase. Missing power/work, long gaps, counter reset/decrease, changed identity/wait, faults, missing safety telemetry, unsafe thermal state, detected game or unknown game state reject the profile. Missing data cannot create an eligible zero. Explicit measured zero power/work is retained when coverage is otherwise complete.

Power uses sample-and-hold from each sample to the next, with no tail extension or missing-interval interpolation. Exact watt-milliseconds remain a numerator over **3,600,000,000** for kWh. The complete sampled run's time/energy budgets include warm-up and inter-window transitions. This audit covers the first-to-last supplied samples, not unrecorded setup/shutdown outside that horizon; it is an admission ceiling, not a live actuator deadline or physical energy guarantee.

Each ordered window names a start, a completed-work boundary after warm-up and a completed-work end. Warm-up work/energy is excluded from the measured rate but retained in total overhead. The actual warm-up/measurement duration may exceed the requested minimum by at most the maximum gap. Work is never fractionally apportioned across a batch to hit an exact timer. Windows cannot overlap, skip a requested candidate/repetition or silently change applied wait.

Per candidate, the mean is **sum of completed user work / sum of actual measured elapsed time**; unequal-duration trials are not equally weighted. Donation work remains separate while its elapsed time stays in the denominator. Each repetition's exact rate and observed minimum/maximum are retained. Those extremes are **not statistical confidence bounds**; `statisticalConfidenceAvailable` is false. Warm-up/transition time, user/donation work and energy are separately conserved as overhead. No throughput, efficiency or positive-profit claim selects a winner here.

## Eligibility and stop

`WorkProfileUse` is a single-owner eligibility latch borrowing an already explicit `SessionIntent` token. User stop takes priority and a later session start cannot revive that instance. Fault, drift, clock reversal, expiry, disabled plan or any full-plan fingerprint change latches refusal. A transient thermal/telemetry or game constraint withholds normal-profile eligibility; clearing it does not override a prior latch. Normal measurements never establish game coexistence qualification.

This in-memory latch is not a durable profile revocation registry. Runtime ownership must propagate drift/identity changes, retain invalidation across its own object replacement, and require appropriate new measurements/review. There is no automatic restart, profile revalidation, sampling or GPU control.

## Verification and remaining work

Tests independently check unequal-duration weighted work rates and conservation of total/measured/overhead energy/time, donation separation, exact budget boundaries, missing/zero distinction, every identity/settings axis, maximum plan shape, monotonic-origin independence, clock/counter/coverage refusal, game/safety rejection, cancellation, immutable provenance and latched stop/drift/expiry.

Executed evidence: Windows/MSVC focused **3/3** (profile, native build recipe and architecture); Windows and Linux/WSL/direct GCC profile **1230 checks** each. The same Linux suite passes **ASan+UBSan**, compiling the new implementation/tests with instrumentation and reusing the unchanged instrumented exact-decimal object. The initial 1218-check run passed; final review added full measurement-plan change invalidation and twelve checks before the final runs. The final documentation guard caught prose in a status enum cell; the explanation was moved below the table. These are offline host/fixture results.

Next: an authorized bounded live measurement owner, completed-work/sensor and drift adapters, persistent private profile/review lifecycle, native controls, qualified game baselines and production GPU validation. The economic selector can consume these exact fixture contracts separately; real revenue and tariff qualification still govern any production optimization.

[P-03h/P-10e](worker-wait.md) supplies an actual shared-worker-boundary wait-installation receipt. It does not fill in the completed-work/power samples, start measurement windows or qualify a profile; those owners must bind the full original receipt and subsequent observations explicitly.
