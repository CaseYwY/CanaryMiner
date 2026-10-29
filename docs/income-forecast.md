# Exact calendar-day forecast composition (P-08j)

This pure development module composes [posted-credit facts](credit-ledger.md), explicit [civil time / electricity arithmetic](electricity-core.md), calibrated user-work integrals and latest [price/FX references](market-data.md). R-INC-001–007, R-ENG-001 and the net-view requirements remain partial; M-FORECAST/M-LEDGER/M-ENERGY; T-FORECAST/T-LEDGER/T-ENERGY/T-UI; D-44.

**Only synthetic earning/work qualification is accepted.** No real pool has acquired a qualified credit/earning-alignment contract by being passed to this module. The library is separate from ordinary miner startup. The native dashboard receives its exact prevalidated lines only in the explicitly synthetic preview; ordinary income/forecast activation is still absent. No fetcher, settings toggle, GPU measurement, billing-state writer or optimizer is introduced.

## Exact windows and coherent facts

`incomeForecastWindows` derives the full selected calendar day using explicit UTC, fixed standard offset or the already configured private version-bound IANA database. It never substitutes the OS zone. A day is `[begin,end)` and uses its actual elapsed length, including DST.

The credit source has integer-millisecond posted events and `(fromExclusive,throughInclusive]` queries. For report instant `now`:

- Today actual query is `(day.begin - 1, now]`, including an event exactly at local midnight.
- Rolling actual-24h is `(now - 86400000, now]`, independent of the calendar day.
- Cumulative is the existing cumulative query, whose actual lower bound remains the source's declared history start minus one. Its IPC request retains the required zero placeholder rather than inserting a nonzero cumulative `from`.
- Future schedule is `[now + 1, day.end)`. Thus the included posted millisecond and future work do not overlap, the final included millisecond has zero remaining time, and the next millisecond belongs to the next calendar day.

The core checks each total's exact interval, requested/covered duration, qualification, scope, decimal scale, timestamps, completeness and optional complete amount. Unknown is not zero. Partial known quantities remain separately visible and retain their original coverage; they cannot form a complete today forecast or net value. Negative posted-window adjustments remain valid actual facts. A negative cumulative total is inconsistent with the current ledger contract and rejects.

`ForecastCredits` carries the committed source sequence/snapshot ID alongside its source/observation times and exact binding. Today/24h/cumulative must come from one coherent source snapshot. Calibration periods must also share one source snapshot. These are caller-supplied **committed read identities**, never queue tickets, Prepared target sequences or an authentication proof for arbitrary caller data. A future owner must attach them to the same validated helper read/checkpoint. A new source snapshot makes an older reward model stale until explicitly recalculated; revisions cannot silently mix old and new period sums.

## Calibrated expected work, not share allocation

`calibrateReward` accepts a bounded set of complete, non-overlapping, chronologically ordered periods, each with exactly matching scope/binding and a fully covered user-work timeline. An explicit `fixture.*` earning/posting alignment contract is mandatory. Posted timestamps alone do not establish when work earned a credit or how delayed pool credits should be distributed. Real source/reward and measured-profile qualification remains unimplemented.

Work rates are explicit **user TMAC/s**. Multiplication by elapsed milliseconds produces a TMAC·ms/s integral; the common `/1000` cancels in the ratio. The model retains exact sums:

```text
reward = sum(complete period net posted tokens) / sum(matched user work)
remaining_tokens = reward * planned remaining user work
today_tokens = complete actual credited today + remaining_tokens
```

There is no average-of-period-ratios, wall-balance, payout, pending reward, accepted-share or assumed earning-duration input. Positive tokens with zero matched work reject. A fully covered zero-token calibration with positive work is a legitimate zero model. Negative-period net adjustments are retained by the ledger but cannot train a negative reward coefficient.

The resulting `RewardModel` has private immutable validated input/sums and an explicit validity deadline. The future schedule must match the exact model profile and scope, be recently declared, and cover the complete remainder without gaps/overlap. User work, donation, pauses and switching are explicit slots. Non-user slots have zero user-work rate; donation and pool losses already present in the calibration are not deducted a second time. Profile keys are qualification assertions in this fixture boundary, not a substitute for P-09's real measured fingerprint/invalidation.

Limits: at most 64 calibration periods, at most 4096 intervals across calibration and 4096 in a future schedule; each calibration period and model validity at most 30 elapsed days; schedule/FX maximum age explicitly 1 ms–24h; reported calendar day at most 48 elapsed hours. Rates use at most 38 digits/18 decimals. Empty, unknown, expired, overlapping, substituted or oversized inputs refuse instead of inventing productive time or a zero quantity. Pure time checks do not supply a cross-call monotonic clock-fault latch; a future lifecycle owner must preserve that guard and independent refresh/retest timing.

## Latest reference value and optional estimated net

The existing quote validator checks identity, both-clock age and independent token/FX feeds. PRL/USDT and USD/USDT remain distinct; USD requires their valid product. `valuationUtcMs` defaults to the report instant and may advance within that same civil day; newer quotes revalue the unchanged report/credit facts. Model/schedule/currency-FX expiry still checks the advancing valuation clock. Valuation before the report rejects as a clock fault; crossing the report's day boundary requires a fresh report rather than relabelling yesterday as today. Missing/stale FX can leave USDT available while USD/net is unavailable. Ticker observation time is not a manufactured trade-event time or optimization qualification.

`ForecastRatio` stores bounded exact decimal numerator and positive decimal denominator. `ExactDecimal::formatRatio(ExactDecimal,...)` performs bounded long division with final round-half-even only (0–24 display decimals); it does not store a rounded reward coefficient. Existing integer-denominator APIs/limits remain unchanged. Overflow refuses; no binary floating conversion is used by the new native projection.

Electricity is optional and separate from revenue:

- No configured electricity input means **no net row**.
- Configured zero/negative energy prices remain configured and meaningful.
- The full-day power scenario must cover exactly the same calendar horizon and explicitly attribute itself to the same production binding. Only synthetic tariffs/power are accepted in this slice.
- The existing tariff evaluator enforces declared rules, effective/publication times, gaps and scope. GPU-board, whole-system and direct incremental-wall bases keep distinct labels. Whole-meter total charges cannot be substituted for mining increment; they reject here pending full counterfactual/continuation integration.
- Non-USD tariffs need a positive, fresh, explicitly sourced USD-per-tariff-currency conversion. No currency symbol inference or unit peg is supplied.
- Net subtracts the exact converted scenario cost from the exact latest-price today forecast over a common denominator. Partial revenue, missing rates, mismatched horizons or unavailable FX cannot produce a net number.

This cost is an explicitly estimated **full-day scenario**, not a persisted accrued actual utility bill. It does not advance monetary billing state, invent measured openings, certify an incremental physical baseline, infer a sale or report actual dollar receipts/current holdings.

## Terminal projection and verification

`incomeForecastDashboardView` supplies prevalidated exact lines to the existing `FinancialView` section. It preserves separate actual-today, actual-24h, cumulative, remaining and full-day forecast rows, source interval/coverage, configured clock, reference series/age and cost basis. Raw private scope/model/plan IDs are omitted. The older synthetic-double display contract remains available when exact lines are absent. The existing dashboard minimum is **60 columns**, including requests below 60; no new 40-column support is claimed.

The CPU fixture uses a real synthetic credit ledger for midnight/rolling/cumulative boundaries and calibration periods. Tests check weighted work sums, immutable source read identity, corrections/negative windows, complete zero, missing/partial coverage, model/schedule scope and expiry, quantity/interval/capacity limits, quote-only revaluation, cost visibility, negative rates, currency conversion and whole-meter refusal. Named-zone tests run full 23h/25h forecasts and energy estimates with exact half-even display. Independent Python `Fraction` compares exact numerator/denominator results and displayed today/value/net outputs across 1500 generated cases; the decimal oracle independently checks arbitrary denominators across another 1500 cases. Native preview coverage spans six modes and three supported widths.

Executed evidence (2026-10-29):

- Windows/MSVC: **63/63 distinct default-off focused CTest cases** across the new forecast trio, existing 56-case regression, runtime/layout pair and collector/lifecycle pair. The final forecast/decimal selection is **5/5** and the collector remainder **2/2**. The optional private-IANA build passes **9/9** forecast/civil/electricity checks. Direct forecast counts are **112** ordinary and **127** with named-zone cases.
- Linux/WSL/GCC: the same forecast **112/127**, each also with own-code **ASan+UBSan**. The external date-tz object is not instrumented. Linux uses direct GCC with warnings as errors, not Linux CMake. Final source includes independent report/valuation clocks and unavailable-scope labels.
- Both platforms: decimal **20,361 checks**; forecast Fraction oracle **1500 cases / 9000 exact-and-display comparisons**; decimal Decimal oracle **1500 cases / 9000 comparisons**; native dashboard **18 mode/width scenarios** plus the below-60 clamp. Linux sanitizer builds pass both oracles and the native projection too.
- Linux rebuilt regressions: display **77**, settings **152**, synthetic supervisor **246**, electricity **149**, market adapters **87**, credit ledger **99**; native settings, session-energy **24** and manual-market **20** PTY checks pass. Python discovery **123 tests with seven explicit fixture-dependent skips**. Ordinary Windows/Linux host artifacts exclude the forecast projection/fixture marker. Architecture contracts and staged privacy/artifact review are separate checks.

Initial harness fixes covered an explicit `shared_ptr`-to-bool assertion, a private recipe's source filename, unused probe harness functions, and the existing 60-column minimum. A Windows regression wrapper accumulated repeated developer-environment setup until its command line was too long after the completed 56-case/runtime checks; the unrun collector remainder was rebuilt and passed in a fresh bounded invocation. That wrapper is not reported as a complete successful run. No source-data, physical GPU, production miner, supplier, kernel/disk-failure or runtime forecast-owner qualification is inferred.

Reproduction:

```text
cmake --preset tests
cmake --build --preset tests --target test_income_forecast income_forecast_probe income_forecast_preview test_exact_decimal exact_decimal_probe
ctest --preset tests -R "^(income_forecast.*|exact_decimal.*)$" --output-on-failure
```

Remaining work: source/earning alignment and physical measured-work qualification; coherent helper query/checkpoint adapter and bounded runtime owner; monotonic lifecycle/fault latching; persisted monetary bill continuation and whole-meter counterfactual integration; settings/reporting-clock/assumption controls; actual-income native activation and production deployment. This batch advances P-08 and R-INC-002 to a tested pure composition boundary, not a complete income product.
