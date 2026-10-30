# Exact offline economic candidate studies (P-10a)

M-OPTIMIZER/M-PROFILES; R-OPT-001/003, R-TAR-005, R-PRO-001/002, R-SAFE-001/002 and R-PRIV-002; T-OPTIMIZER/T-PROFILES/T-PRIVACY; D-60. `EconomicStudy` retains immutable inputs and exact candidate arithmetic. It is disabled by default and admits only qualified **synthetic fixtures**. It supplies no scheduler, mining permission, telemetry, network access, persistence or ordinary UI/runtime activation.

## Exact measured record and attribution

One study compares the ordered points of one audited [work profile](work-profile.md). Its current full plan must still match; clock reversal, expired evidence, drift, faults, missing/unsafe safety telemetry and active/unknown games refuse a study. This is a normal-work model; it cannot establish coexistence permission.

The reward model must explicitly name `economicProfileKey(profile)`, a versioned hash of the complete plan, run/counter epochs, every raw sample and every window. A reusable plan key or a model-name match is insufficient. Changing measured data changes this key even when all settings stay the same. The immutable [reward calibration](income-forecast.md) must match the complete synthetic worker-scoped credit contract. Account totals are not allocated to a device. No shares, balances, withdrawals or manual point quotes can substitute for a qualified earning/work calibration.

## Market and electricity boundaries

The economic price window defaults to **480 closed minutes**, independently adjustable from **1–719 minutes**. It ends at the last completed UTC minute at the decision instant. Both PRL/USDT and USD/USDT candle series must completely cover the same interval, have the expected generation and fixture minute-start convention, and pass both wall/monotonic observation-age and cross-feed-skew checks. Defaults are 120 seconds observation age and 30 seconds feed skew; bounds are one day. Historical USD uses **mean(P_i × F_i)**, retaining exact sum/count. USDT is never treated as USD, and refreshing observations cannot fill missing candle intervals. This window is independent of the dashboard average.

This first cost adapter is explicitly a **GPU-board flat-rate proxy**. It requires an independently configured, fully identified synthetic tariff, matching power evidence, explicit opening at the future scenario start, one published constant-rate revision covering the horizon, no tiers/calendar/dynamic rules, fees, floor or non-unit multiplier. UTC/fixed clocks are supported. Non-USD tariffs require a separately qualified, fresh, explicit USD-per-tariff-currency reference; even USDT requires it. Signed/zero rates are meaningful. Absent configuration is unavailable, not free electricity.

Combined tariffs and whole-meter incremental cost are deliberately unsupported by this adapter: their eventual comparison must use matched full trajectories and the common opening/cycle, not flatten their rates or allocate a shared allowance to each GPU. Board estimates do not establish whole-PC profit, a utility bill or actual dollar receipts.

## Horizon, overhead and exact scoring

Each study has one explicit **1 ms–24 hour** horizon `[decision+1, decision+1+horizon)`, default one hour. Every permitted point needs a fresh, independently declared transition assumption, even when transition time/energy is explicitly zero. Optional `repeatMeasurement` forecasts one new repetition of the entire supplied profile's sampled experiment: all its measured/warm-up/transition time and energy are charged once as prospective test overhead. Previously incurred tests are not retroactively charged unless this future repetition is selected. Unrecorded setup/shutdown remains outside the profile and must be included in the declared transition when applicable.

After transition/test time, the remaining scenario runs at the candidate's measured weighted mean. Let `H` be horizon milliseconds, `R` remaining run milliseconds, `M` measured milliseconds, `W` completed user TMAC, `E` measured watt-milliseconds, `O` future transition/test watt-milliseconds, and calibration `C/A` be credited tokens divided by TMAC/s × ms:

- Expected tokens = `C × W × 1000 × R / (A × M)`.
- Estimated kWh = `(E × R + O × M) / (M × 3600000000)`.
- Reference revenue = expected tokens × exact window USD mean × `(1 − haircut)`.
- Cost = estimated kWh × signed flat tariff × explicit currency conversion.
- Score = `(revenue − cost) × 1000 / H`, estimated USD per actual elapsed second.

The measured user-work rate already includes donation elapsed time; donation is not deducted twice. Transition/test work receives no forecast revenue in this conservative scenario. The optional bounded `[0,1]` revenue haircut is a heuristic, not a statistical confidence bound or another pool fee. Expected tokens remain the un-haircut model projection; only revenue is adjusted. A complete zero-credit calibration remains an explicit zero; missing calibration remains unavailable.

All arithmetic remains bounded exact decimal numerators/denominators. Nonterminating means are not rounded before cost or ordering. Comparisons cross-multiply and refuse capacity overflow; a failure in any permitted candidate invalidates the complete comparison, rather than declaring another candidate best from incomplete results. Explicitly excluded candidates are not scored, but retained input sizes remain bounded. Exact ties keep the earlier explicitly ordered point.

Only a strictly positive score supplies `bestPoint`, labelled **best measured eligible candidate**, never global optimum. A valid study with no positive candidate (including all excluded) has no selection. An unavailable study cannot justify continuing or resuming mining. The immutable record is an offline result, not a live generation/revision-bound handoff; session ownership, ongoing revalidation, stop precedence and hysteresis remain subsequent work.

[P-10c](economic-study-use.md) subsequently adds a one-result explicit-parent validity gate, original dependency expiry and terminal current-plan/condition/stop revalidation. Live ticket admission, selection hysteresis and actuation remain separate.

## Verification and remaining work

The host suite checks hand-calculated ranking changes between throughput and energy-efficiency preferences, missing/zero/signed cost and income, correlation-preserving price conversion, exact transition and future test costs, record/plan/source identity, safety/game/freshness refusal, maximum window, invalid excluded-input bounds, and arithmetic comparison overflow. An independent Python `Fraction` oracle varies durations, work, power, currencies, prices, horizons, overhead, repeat tests and haircut; it checks all twelve rational outputs and the selected point.

Executed evidence: Windows/MSVC focused **4/4**, both platforms **120** host checks and independent Fraction oracle **192 cases / 2496 exact comparisons**. The same Linux/WSL/direct GCC host/oracle selection passes **ASan+UBSan**, rebuilding the economic/forecast/market/credit units and reusing unchanged instrumented profile/arithmetic/time/transport objects. Initial **114** checks passed; final review bounded retained excluded inputs and added maximum-window, FX and unrepresentable-ordering cases before the final runs. Architecture and import guards pass.

Runtime cadence/price refresh/retest controls, hysteresis/cooldown/switch budgets, whole-meter adapters, real source qualification, authorized physical measurements and native integration remain open.
