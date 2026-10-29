# Data and calculation contracts

## 1. Facts, estimates and availability

Every value carries a unit, scope, time interval, measurement/valuation basis, provenance, freshness and coverage. Availability is not encoded as numeric zero: distinguish `available`, `partial`, `unavailable`, `stale`, `unsupported` and `invalid`.

Store instants in UTC and use monotonic time for durations/deadlines. Use an explicit named billing/reporting timezone for calendar calculations; never infer currency, tariff or timezone from translated UI labels. Durations crossing DST use actual elapsed seconds. Storage schema versions and migrations are explicit.

Financial/token storage uses validated decimal or integer atomic units with an explicit scale. Do not guess Pearl precision, silently round to display precision, accept NaN/infinity, overflow counters, or conflate W, kW and kWh. Negative retail energy prices and credit reversals can be legitimate; negative market asset prices are not.

## 2. Accepted history

Input contract: `installation_scope`, `worker_generation`, cumulative acknowledged accepted **user** count, observation instant, and optional exact upstream acknowledgement timestamps. Developer-donation counters stay separate.

- Difference counters only within the same generation. Handle restarts/carry explicitly; a decreased counter is not an unsigned subtraction and must not fabricate a lifetime jump.
- The same observation/replayed durable record is idempotent. Multiple GPUs aggregate once; rejected/stale/unknown/pending work never becomes accepted history.
- Rolling windows use `now - duration < observed_time <= now` for 24h, 7d and 30d, with any storage-bucket resolution declared. “All” means all retained local-history aggregates since a known collection start, not lifetime mining before collection began.
- Persist atomically/transactionally, retain all-time aggregates during compaction, and show unavailable/corrupt state rather than initialize an unreadable lifetime history to zero.
- Report collection start, gaps, timestamp basis and any bucket resolution. Observed acknowledgement time is not an invented pool timestamp. Clock rollback/forward corrections must not double count or silently imply complete windows.
- Accepted shares can differ in difficulty; counts are operational statistics, not a token-income formula or a fair cross-pool work comparison.

## 3. Market snapshot

Required identity: coin/network/asset reference, provider, trading pair, quote currency, bid/ask/last or candle-close definition, observation/publication time and valid interval. Matching a ticker alone is insufficient.

The main panel displays two distinct values:

1. **Last-1h equal-time average:** complete, equally spaced observations/candles over the declared hour. A mean of minute closes is labelled a TWAP approximation, not a volume-weighted price. Missing/duplicate/incomplete candles fail coverage validation; sparse feeds need explicit time weighting.
2. **Latest quote:** the most recent valid quote and its age/source. It is not the 1h average.

If the pair is token/USDT, retain USDT explicitly. USD output requires a fresh, valid USDT/USD quote:

```text
token_price_usd_now = token_price_usdt_now * usdt_price_usd_now
```

Never hard-code `1 USDT = 1 USD`. Missing conversion can leave a valid USDT display while USD is unavailable. Independent display and optimization lookback windows must not overwrite each other. Requests, pagination, JSON sizes, redirects, source switches, cache age and retries are bounded; provider errors never disable certificate validation.

## 4. Actual income and latest-price valuation — final decision

Actual income is **attributable pool-credited token production**, not the wallet balance, a payout, an accepted-share count, a future estimate or a sale. A provider record needs a stable ID/revision, token amount, earning/credit time basis and attribution scope.

- Show actual last-24h and cumulative credited token quantities only over known provider/local coverage. If only wallet-level data exists, do not label it as one worker's income; require a clearly disclosed account-level view or leave worker income unavailable.
- An exclusive-account assertion needs explicit local configuration and visible scope; it must never be inferred from a single currently visible worker.
- Deduplicate API pagination/retries and handle revisions/reversals without adding the same income twice. Payouts move already-earned funds and are not new revenue.
- Late-discovered historical income retains its actual source interval. It does not become “earned now” merely because the program first saw it now.

The **main panel** uses current prices:

```text
actual_tokens_24h = sum(attributable credits in the rolling 24h interval)
actual_tokens_all = sum(attributable credits over the declared cumulative coverage)
production_value_usdt_now = actual_tokens * token_price_usdt_now
production_value_usd_now = production_value_usdt_now * usdt_price_usd_now
```

Market updates change these derived reference valuations, **not** the actual token-credit facts. Use labels such as “production at latest price” or “reference valuation”. Do not label them actual dollar receipts, realized profit, current wallet assets or current holdings. Even if all produced tokens were sold, historical production remains historical production.

Sale status is **unknown**. Pool credits, wallet transfers and exchange deposits cannot prove a trade. No exchange/account connection, trade import or selling action is required for this product. A future explicit trade-import feature would be separate scope and must never be inferred from these metrics.

Optional per-credit historical estimates may appear in Details. They are immutable reference observations with source/time/basis, not proceeds or tax determinations. If recorded at discovery time, call them discovery-time estimates, not historical production-time prices. They do not replace the main latest-price total or the optimizer's fresh prices.

## 5. Today's forecast

“Today” means the full calendar day in the configured billing/reporting timezone, which may contain 23, 24 or 25 elapsed hours:

```text
today_forecast_tokens = actual_credited_today_so_far + expected_tokens_for_remaining_today
today_forecast_value_usd = today_forecast_tokens * latest_valid_token_price_usd
```

Remaining income uses eligible productive time, current/local measured throughput and an explicitly calibrated expected reward model. Include expected pauses/switch downtime and the unchanged donation setting without double deduction. Pool fees/rejections already included in a calibration must not be deducted again.

When estimating a reward-per-work coefficient from pool history, align complete income periods with the **same attribution scope** and elapsed-time work integral:

```text
expected_reward_per_work = sum(credited_tokens_for_complete_periods)
                         / sum(matched_scope_rate_integrated_over_those_periods)
```

Wallet-wide income may calibrate only against wallet-wide work, not this machine alone. Such an aggregate model may support a labelled local forecast but still does not establish actual per-worker income. Exclude incomplete/misaligned periods, retain calibration coverage/fees/basis and handle a valid zero-income period honestly. Never derive the coefficient from wallet balance, unrelated payouts or a guessed duration.

Declare the future-load/activity, price, reward and schedule assumptions. Unknown future game sessions, dynamic electricity intervals or pool-credit delay are not facts. If actual credited-today coverage is missing, do not fabricate a complete “today total”; show unavailable/partial and optionally a separately named current daily-equivalent estimate.

Rolling actual-24h income and today's calendar forecast are independent rows with different intervals. A pool's daily buckets may be insufficient for exact rolling 24h; show the available interval/partial status rather than invent per-hour allocation.

## 6. Energy, electricity and estimated net return

```text
energy_kwh = integral(power_watts * elapsed_seconds) / 3_600_000
incremental_cost = Bill(non_mining_load + mining_load, opening_state)
                 - Bill(non_mining_load, opening_state)
estimated_net_today_usd = today_forecast_value_usd - estimated_incremental_electricity_today_usd
```

Both bill branches use the same opening state and horizon and must not mutate the live ledger. Multiple miners share allowances, peaks and generation: their incremental costs need not be additive. Fixed charges cancel only if unchanged; capacity upgrades, minimum bills, lost credits and eligibility changes may prevent cancellation.

- **No electricity configuration:** omit net-profit rows completely. A legitimately configured zero/negative rate is not “not configured”.
- **Configured but missing inputs:** show unavailable/reason, not zero cost/profit.
- **GPU-board proxy:** permitted only as an explicitly labelled estimate with exclusions. It is not wall power, whole-PC profit or isolated mining incremental power during a game.
- **Whole-system/incremental wall measurement:** retain adapter identity, matched baseline, timing and coverage. Missing intervals or invalid sensor units cannot be filled with zero.
- Convert tariff-currency cost to USD with a valid explicit FX rate/time/basis. No currency inference from a symbol or UI language.
- Default net row is “Estimated net today”, with the same horizon as today's forecast. Optional other horizons require separate labels and matched revenue/energy intervals.

See [international billing research](../research/international-electricity-billing.md) for progressive versus all-units, signed rates, TOU, calendar/effective-date, tax/demand, prepaid and export/credit contracts. Not every researched structure is implemented. Unsupported rules require a declared simpler estimate or no cost-based scheduling.

The initial [observed-energy ledger](../energy-history.md) integrates raw integer milliwatts over mapped monotonic intervals with no per-interval rounding. Queries and explicit independent reset windows are half-open `[begin,end)` and retain missing/prehistory/inter-session/tail coverage. A new measurement session cannot reuse a prior parent session's monotonic domain or continue its last power value. Reset windows only select accumulated observations; they never erase the source history, manufacture a measured zero opening or feed a board-power register into a whole-meter bill. Input origin/basis and private channel binding remain immutable per store.

[Durable energy recovery](../energy-recovery.md) retains the exact observation batch and its **prior committed** sequence/hash before append. Neither Prepared's target sequence nor a queue ticket contributes energy. Restart inspection accepts only that prior prefix or the exact original's resulting head; successful explicit retry does not renew observation times, add duplicate integrals or authorize continuation of an old sampling session. A failed retry leaves unresolved Prepared evidence intact.

[Opt-in exact compaction](../energy-compaction.md) preserves summed milliwatts and covered-channel counts as exact piecewise intervals, with active per-channel continuity retained separately. Arbitrary millisecond queries and independent reset projections clip those intervals without rounding or average-power interpolation. Raw per-channel originals before the exposed compaction boundary expire; DetailExpired is neither exact replay proof nor missing aggregate energy. Segment growth remains bounded and capacity rejects instead of weakening precision or coverage.

## 7. Curves, calibration and optimization

Profile identity includes local device identity, software/driver/kernel version, work shape, throttle mechanism, thermal environment, measurement basis, warm-up/repetition/window and evidence freshness. Never copy one machine's frequencies, identifiers, power or profit numbers into shipped defaults.

Score measured candidates by expected net return per actual elapsed time, including pauses, transitions and test energy. Eligibility requires safe hardware conditions, valid data and, for coexistence, an experience-qualified game profile. Reject invalid/expired curves; name a winner “best measured eligible candidate”, not a global optimum.

Separate telemetry cadence, benchmark warm-up/duration/repetitions, market lookback/refresh, policy evaluation, data-expiry/retry and recalibration clocks. Permit user adjustment within capability/rate/safety limits. Use hysteresis/cooldown/switch budgets to avoid flapping. A configurable conservative margin is a heuristic unless supported by a documented statistical model.

Requested initial economic schedule: query on explicit start, then hourly using an 8h economic price window, with both interval and lookback configurable. This is independent of the main dashboard's 1h average and independently bounded telemetry/test timers; it is not a private electricity or GPU default.
