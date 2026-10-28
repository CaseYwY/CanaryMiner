# Pearl market and pool-credit provider research

**Evidence reviewed:** 2026-10-28. **Status:** adapter design research, not an implemented integration or a live-data qualification.

This note supports `R-MKT-001`–`003`, `R-INC-001`–`007` and `R-OPT-002` in [requirements](../architecture/requirements.md). The controlling definitions are [data and calculations](../architecture/data-and-calculations.md), especially §§3–5 and 7. HTTPS implementation must preserve the boundaries in [secure transport backends](secure-transport-backends.md), §6.

**Research boundary:** first-party public documentation, published schemas and unauthenticated instrument metadata only. No wallet/account endpoints were called, including with invented or example addresses. No account, credentials, mining connection, trading operation, port scan or installation was used. No ticker, candle, trade or earnings API response was requested. Examples and financial values below are entirely synthetic. Incidental figures in documentation/search results are not evidence for a price or revenue claim. Agent-reach supplied Exa discovery and its public-page reader route; Context7 located Gate/Kraken API documentation. The cited first-party contracts, not generated summaries or third-party adapters, control the findings.

## 1. Recommended first slice

| Role | Concrete candidate | Recommendation and qualification limit |
| --- | --- | --- |
| Pearl price history | Gate international Spot API v4, `PEARL_USDT`, `GET /spot/candlesticks`, explicit `interval=1m` | **Recommended primary candidate.** Identity is supported by an official listing announcement and instrument metadata. The documented eight-position candle contract includes a closure marker, but its seven-position example is inconsistent. Resolve that discrepancy and timestamp-boundary semantics before claiming production-qualified complete-minute coverage.[^gate-listing][^gate-meta][^gate-spot] |
| Latest Pearl reference | Gate `GET /spot/tickers?currency_pair=PEARL_USDT` | Use `last`, explicitly labelled **last-trade reference, observed at**, not a minute close or executable sale price. Ticker has no general last-trade timestamp; receipt age is not trade age. Keep bid/ask as separately defined optional observations.[^gate-spot] |
| Direct USDT/USD conversion | Kraken Spot REST, request pair `USDTUSD`; metadata result key `USDTZUSD` | **Recommended FX-market candidate.** This is a direct USDT-base/USD-quote market, not a USDT peg assumption, USD/USDT inversion or USDC proxy. Use public Ticker for a current observed last-trade reference and public OHLC `interval=1` for aligned historical conversion.[^kraken-pairs][^kraken-meta][^kraken-ticker][^kraken-ohlc] |
| Actual PRL credits | Kryptex public Pool API | **Do not enable an actual-credit ledger from the currently published schema.** Reward-chart aggregates lack a credit ID/revision, worker attribution and defined credit-time/finality semantics. Exact rolling 24h and complete cumulative attributable income are not established. Report the capability as unsupported/unavailable; do not substitute balance or payouts.[^kryptex-schema] |

Implement replaceable `MarketPriceAdapter`, `FxAdapter` and `CreditProvider` contracts behind the bounded public JSON client. These are proposed interface names, not existing code. Start with offline fixtures. Runtime providers should remain opt-in and subject to access/terms review; this recommendation is **not legal clearance to ship enabled-by-default third-party data access**. Manual/offline inputs remain separate, explicitly sourced alternatives, never silently fabricated provider observations.

The two exchanges produce a cross-venue **reference valuation**. Their quotes do not guarantee an executable conversion, matching liquidity, redemption at par or sale proceeds. Changing either provider changes provenance and requires an explicit source transition, not a splice hidden inside an existing average.

## 2. Instrument and network identity

### Pearl: why `PEARL` at Gate means this project's `PRL`

1. Gate's announcement **“Gate to List Pearl (PEARL) for Spot and Convert Trading,” 2026-10-23**, names the spot pair `PEARL/USDT` and opening time **2026-10-23 13:00 UTC**. Crucially, it links the project to `https://pearlresearch.ai/`, the `prlnet` social identity and **`https://explorer.pearlresearch.ai/?network=mainnet`** as its token identity reference.[^gate-listing]
2. Pearl Research's own Network page describes a **native digital currency**, links its network repository, and refers to the currency mined during inference as **PRL**.[^pearl-network]
3. Kryptex's own Pearl pool page uses **PRL**, names **PearlHash**, and links the Pearl Research Labs desktop wallet repository. This provides a pool-side identity bridge, not just a ticker match.[^kryptex-prl]
4. The public Gate instrument-metadata reads on the review date confirmed these selected identity fields only:[^gate-meta]

   | Metadata resource | Relevant identity fields |
   | --- | --- |
   | `/api/v4/spot/currency_pairs/PEARL_USDT` | `id=PEARL_USDT`, `base=PEARL`, `base_name=Pearl`, `quote=USDT`, `quote_name=Tether`, `type=normal` |
   | `/api/v4/spot/currencies/PEARL` | `currency=PEARL`, `name=Pearl`, `chain=PEARL`; chain entry `name=PEARL`, empty contract-address field |

**Conclusion:** record an explicit reviewed mapping from the application asset **Pearl network mainnet native currency, local alias PRL** to Gate's asset **PEARL** and pair **PEARL_USDT**. Store the project/explorer references with the mapping. Do not use a global symbol lookup for `PRL`, automatically substitute another `PEARL` token, infer an EVM contract from an empty address, or use Gate pair price/amount precision as Pearl's atomic-unit precision.

This establishes **documentation-level identity**, not cryptographic verification of a genesis hash, custody backing, a deposit/withdrawal route or token atomic precision. Those properties were not established by these sources. Gate's listing time is a lower bound on this venue's history; it does not promise a candle for every minute after opening, and it does not provide pre-listing history. Metadata status is a point-in-time fact, not an uptime or future-listing guarantee.

### USDT/USD: orientation and aliases

Kraken's public metadata read `GET /0/public/AssetPairs?pair=USDTUSD` returned the reviewed identity mapping `USDTZUSD`, `altname=USDTUSD`, `wsname=USDT/USD`, `base=USDT`, `quote=ZUSD`, with currency asset classes. Thus the rate has unit **USD per USDT**. `ZUSD` is an internal fiat asset identifier, not a second stablecoin.[^kraken-pairs][^kraken-meta]

Omitting `assetVersion` uses Kraken's internal names. Its documentation also offers `assetVersion=1` for display names and slash-separated pair keys. Pick one mode consistently; the initial recommendation is to omit it and validate the metadata-derived key. Never assume the response property equals the request string, pick the first object member, or confuse the OHLC cursor `last` with an instrument. A future naming-mode change must have its own fixtures.[^kraken-pairs][^kraken-ticker][^kraken-ohlc]

USDT here is an exchange market asset; no transfer network is being selected. This note does not equate distinct token contracts or authorize cross-chain transfers. No on-chain balance, redemption or deposit endpoint is necessary for either market adapter.

## 3. Gate Pearl adapter contract

**Reviewed origin:** `https://api.gateio.ws`; **prefix:** `/api/v4`. Use the international documentation, not Gate US/EU examples with different hosts and potentially different contracts. All endpoints in this section are documented as unauthenticated.[^gate-general][^gate-spot]

### Complete one-minute candles

| Contract item | First-party evidence / adapter rule |
| --- | --- |
| Request | `GET /api/v4/spot/candlesticks?currency_pair=PEARL_USDT&interval=1m&from=S&to=E`, with integer Unix-second query bounds. `S`/`E` here denote request bounds, not an assertion that the server uses a half-open interval. |
| Limit | At most **1,000 points per request**. `limit` conflicts with either `from` or `to`; do not send them together. Without `from`, the documented start is `to - 100 * interval`; without `to`, the end defaults to current time. Always set the intended interval and bounds rather than relying on defaults. |
| Response envelope | Array of arrays; published schema is `[[string]]`. There is no echoed pair in each candle: pair identity belongs to the validated request and provider namespace. |
| Position order | **0:** Unix-second timestamp; **1:** quote volume (USDT); **2:** close; **3:** high; **4:** low; **5:** open; **6:** base volume (PEARL); **7:** whether the window is closed. Price units are USDT per PEARL. |
| Closure | Prose says true means complete and false means incomplete. Under the documented string-array schema the proposed dialect accepts exact strings `"true"`/`"false"` at position 7. Do not guess a missing flag or accept arbitrary truthy strings. A JSON-boolean dialect, if confirmed separately, must be explicitly versioned/tested. |
| Time | Query and row timestamps are seconds, not milliseconds. The proposed normal form uses an integer minute-start `t`, interval `[t,t+60)`, and `t % 60 == 0`. **The retrieved REST prose does not expressly define the row timestamp as open versus close time. This interval-start interpretation is a qualification assumption, not a verified vendor guarantee.** |
| Pagination | This endpoint documents time bounds, not `page`, `offset` or an opaque candle cursor. Partition longer requests into bounded time slices and merge by provider/pair/interval/timestamp. Do not apply the API's general pagination discussion to this endpoint. |
| Unspecified | Inclusive/exclusive bounds, row order, empty/no-trade-minute behavior, late revisions, closure-publication delay and an exhaustive historical-retention guarantee were not established by the reviewed REST contract. Never fill their absence with assumed candles. |

All provider facts in the table come from the specific Spot endpoint and general time contract; the normal-form and validation choices are project recommendations.[^gate-spot][^gate-general]

**Material documentation inconsistency:** the prose lists **eight** positions, but the example contains **seven**, ending in `"true"`, with no separate base-volume value. A seven-item numeric-tail array is also not proof of a closure marker. Do not infer position meanings from row length or treat position 6 as both volume and closure. The recommended initial parser rejects ambiguous rows and reports a schema/capability error. Vendor clarification or separately authorized public-data qualification is a release gate; no live candles were fetched here to resolve it. The WebSocket contract separately documents a Boolean `w` closure marker, but it is a different transport/schema and does not repair REST by implication.[^gate-spot][^gate-ws]

### Latest quote, and what its age actually means

`GET /api/v4/spot/tickers?currency_pair=PEARL_USDT` returns an array. Require exactly the expected pair's record, then parse decimal strings `last`, `highest_bid`, `lowest_ask`; the latter two are distinct quote sides, not synonyms for the last trade. Always supply the pair; omission requests all tickers.[^gate-spot]

The published `Ticker` schema has **no general price-event timestamp**. `etf_pre_timestamp` describes ETF rebalancing and is irrelevant to this native Pearl pair. Gate's `X-In-Time`/`X-Out-Time` headers are API-gateway microsecond timestamps, not the last trade time. Record local request-start/receive times and optional gateway times without relabelling them.[^gate-spot][^gate-general]

Consequently:

* A ticker-only output may say **“Gate last-trade reference, observed at …; trade time not supplied.”** Its displayed age is observation age. Frequent successful polling cannot prove that the last trade is recent.
* Do not silently label a candle close as the latest trade or use a 24h high/low/change statistic as the current price.
* If policy requires a maximum **trade-event age**, ticker-only evidence is insufficient. The documented optional upgrade is public `/spot/trades`, whose rows include `id`, `currency_pair`, `price`, `create_time` and `create_time_ms`. The latter is milliseconds and can contain sub-millisecond decimals. Its limit is 1–1,000 and default time search is the last 30 days. Ordering/latest-selection semantics must be qualified before assuming `limit=1` means the newest trade. No trade request was made in this research.[^gate-spot]

An alternative future definition is a timestamped order-book midpoint, not a last-trade price: Gate `/spot/order_book` documents millisecond `current` (response generation) and `update` (last book change). Such a switch must change the published price basis and validate both sides; it must not be an invisible fallback.[^gate-spot]

## 4. Kraken direct USDT/USD adapter contract

**Origin:** `https://api.kraken.com`; **prefix:** `/0/public`. These market-data endpoints do not require an account or API key.[^kraken-public]

### Latest FX reference

Recommended request template: `GET /0/public/Ticker?pair=USDTUSD`.

Validate `error` is an empty array, resolve exactly the expected market key, and read:

* `c[0]`: last trade price, **USD per USDT**; `c[1]` is trade volume, not a timestamp.
* `a[0]`: ask price; `b[0]`: bid price. The remaining elements concern volume.
* `p` is volume-weighted average price for today/last 24h, **not** the requested minute-close equal-time average.

Ticker supplies no last-trade event timestamp. As at Gate, retain `source_event_at = unavailable` and label observation age honestly. Its “today” fields start at midnight **UTC**, independently of the user's reporting timezone. Neither `t` (trade counts) nor `o` (today's opening price) is an event timestamp.[^kraken-ticker]

For a separately qualified timestamp-bearing last-trade definition, Kraken's public `Trades?pair=USDTUSD&count=N` supports 1–1,000 entries with `[price, volume, time, buy/sell, market/limit, miscellaneous, trade_id]`; time is a fractional Unix-second observation in the documented examples. Its `last` is a polling cursor, not the last price or a candle timestamp. Do not interchange Trades cursors with OHLC cursors. This is an optional contract upgrade, not an endpoint called here.[^kraken-trades][^kraken-history]

### Historical one-minute conversion

Recommended request template: `GET /0/public/OHLC?pair=USDTUSD&interval=1&since=S`.

| Item | Verified contract / conservative rule |
| --- | --- |
| Envelope | `{"error": [], "result": {"USDTZUSD": [...], "last": cursor}}` under the reviewed internal-name mapping. An HTTP 200 with non-empty `error` is not valid data. |
| Row | Exactly eight entries: `[time, open, high, low, close, vwap, volume, count]`. Time/count are integers; prices/volume are strings. Close is position **4**, unlike Gate position **2**. |
| Closure | Official endpoint text says the **last row is the current, not-yet-committed timeframe and is always present regardless of `since`**. Exclude that original terminal row from complete-window data, even if the client clock seems to say otherwise. Do not sort first and lose its documented positional identity. |
| Time/cursor | The historical-data guide defines `since` as a Unix timestamp; `result.last` is the ID to use as `since` when polling for new committed OHLC data. Treat it as a provider cursor, not as proof of response/quote freshness. Minute-start interpretation of row `time` still needs explicit contract qualification; the retrieved REST row schema labels it only `time`. |
| Depth | At most **720 recent entries**, including the uncommitted entry: do not promise more than **719 complete** minutes from one response. Older history cannot be recovered by setting an earlier `since`. No arbitrary page/offset, end bound or `limit` is documented here. |
| Incremental updates | Save the returned cursor only after validating and committing the response. Replayed rows are idempotent; an unchanged cursor must not cause an immediate retry loop. The cursor does not bypass the 720-entry retention bound. |

These details are in Kraken's endpoint schema and historical guide. Where the guide describes pagination generally, the endpoint's explicit **“older data cannot be retrieved”** warning prevails over any interpretation that repeated calls can backfill unlimited minute history.[^kraken-ohlc][^kraken-history]

Both **60-minute display** and **480-minute economics** windows fit the documented recent-depth bound, subject to actual complete coverage. A newly configured longer lookback may be unsupported without a sufficiently complete persisted local archive or another independently reviewed historical provider. Do not silently increase the candle interval to make a longer request fit. No guarantee of dense no-trade bars, immutable closed bars, publication delay or historical continuity was established here.

## 5. Complete-window calculations and independent clocks

The following are proposed deterministic project rules, **not vendor SLAs**. They depend on qualifying the minute-start timestamp convention above.

1. Take a fixed UTC evaluation instant `N` and configurable settlement guard `g >= 0`. Define `E = floor((N-g)/60) * 60`. For the display, `S1 = E-3,600`; require the 60 minute starts `S1, S1+60, …, E-60`. This is **the last 60 complete UTC minute bins through E**, not a continuously sliding second-exact hour ending at N. Display the actual coverage end/lag. A guard never substitutes for Gate's closure marker or Kraken's uncommitted-row exclusion.
2. For Gate request each target slice with one guard minute before and a bound at its end, then filter locally to the target minute starts. This avoids relying on undocumented bound inclusion. For example a target `[S,E)` uses request `from=S-60&to=E`, without `limit`; surplus boundary rows are discarded. Use at most 900 target minutes per slice, leaving headroom under 1,000 even with boundary rows. Set a finite total window/page/attempt budget. If a provider omits an expected boundary row, coverage fails rather than advancing the slice silently.
3. Reject duplicate timestamps within a source response, missing minutes, non-minute alignment, future timestamps, malformed/unknown closure, invalid OHLC relationships, non-positive prices or negative volumes. Parse financial decimal lexemes without binary-floating-point accounting. Sort validated rows; require the complete expected key set. Known identical page/retry overlap can be coalesced idempotently before final uniqueness checking; conflicting duplicates invalidate the snapshot, not “last response wins.”
4. For 60 complete Pearl minute closes `P_i`, `display_1h_usdt = sum(P_i)/60`. Label it **equal-time mean of 1m closes / TWAP approximation**, not VWAP. A single `1h` candle close is not this mean. A sparse series cannot pass merely because it contains 60 arbitrary observations.
5. If displaying a historical USD mean, require a complete matching minute set of USDT/USD closes `F_i`: `display_1h_usd = sum(P_i * F_i)/60`. Do not use `mean(P) * mean(F)`, or multiply the old-hour mean by current FX and call it a historical USD average. The latter can only be a separately labelled current-FX re-expression.
6. Latest valuation is different: `latest_pearl_usd = latest_pearl_usdt * latest_usdt_usd`. Retain both observation times, both event times if available, basis and permitted maximum age/skew. USD is unavailable when FX is missing/stale/invalid; a valid USDT price can remain visible. Never hard-code parity or treat a fresh HTTP response as proof of a fresh last trade.
7. The requested **economic lookback starts at 8h (480 complete 1m closes)** and policy evaluation runs on explicit start and then hourly. **Both lookback and interval are independently configurable.** The display stays 1h; a shared cache may serve both queries without sharing a mutable “window” setting. Evaluate the economics window over its own exact interval, and use aligned minute FX if the chosen economic model needs a historical USD mean. Keep current valuation, historical price basis and forecast assumptions distinct.
8. Dashboard quote refresh, candle refresh, economic evaluation interval, economic lookback, cache expiry, retry schedule, telemetry, benchmark timing and recalibration are distinct controls. An hourly policy does not make an hour-old quote fresh. Changing the economics lookback must not overwrite display coverage or rewrite actual credit quantities.

For actual credited production, preserve the existing decision: `actual_tokens * latest_pearl_usdt * latest_usdt_usd` is a **latest-price reference valuation**, not realized income, account assets or proof that any tokens were sold. Availability and provenance propagate independently through each factor.

## 6. Access terms, rate limits and bounded operation

| Provider | Verified public-access/rate facts | Required implementation / release decision |
| --- | --- | --- |
| Gate international | Specific Spot market endpoints say no authentication required. General current documentation states **200 requests / 10 seconds / endpoint / IP**, with `X-Gate-RateLimit-Requests-Remain`, `X-Gate-RateLimit-Limit` and `X-Gate-RateLimit-Reset-Timestamp`. A 2024 official adjustment announcement corroborates that public per-endpoint rule; older/regional 900r/s examples are not the selected contract.[^gate-spot][^gate-general][^gate-rate-change] | Use a much lower local budget, shared across installation feeds; public IP/NAT may be shared. Validate throttle/reset headers and respect 429/Retry-After when supplied. Never increase request concurrency to evade throttling. |
| Kraken | Public REST limits are IP+pair for Trades/OHLC and IP-only for other public calls. Official support says **one request per second or less** stays within the limits; continued requests while throttled can prolong restrictions. No account-tier/private-key counter is relevant to this public-only adapter.[^kraken-limits] | Serialize a conservative shared public-request budget, at most one request per second across Kraken calls, with a slower normal refresh and bounded retry/backoff. Do not shard across IPs or use credentials to recover. |
| Kryptex | Public Pool API docs publish its OpenAPI schema. The reviewed page/schema supply **no numeric rate limit, reward-endpoint cache/freshness guarantee, general retention SLA, credit finality contract or clear data-redistribution license**. The index endpoint alone documents a 3-minute HTTP cache; that is not a reward-chart cache promise.[^kryptex-api][^kryptex-schema] | No production credit polling schedule can be certified from these sources. Obtain a documented contract before implementation. Do not test guesses against a wallet endpoint or treat an absent security scheme as consent to disclose an address. |

**Terms matter even for public endpoints.** Gate's User Agreement covers its API, restricts access to designated interfaces (§1.3(h)), restricts resale/commercial use without written agreement (§10.1(d)), grants a limited personal/non-commercial IP license (§16.2), and provides information as-is without accuracy/timeliness guarantees (§12.6). Kraken's Global Terms, “Our Content” / “Restrictions,” grant limited own-benefit use and restrict distribution/commercial exploitation and scraping. Public API documentation establishes a technical unauthenticated interface; it does not establish an unrestricted market-data redistribution grant.[^gate-terms][^kraken-terms]

Before shipping, review applicable regional terms, commercial mining use, local display/caching/derived-data rights, retention, attribution and redistribution separately for both exchanges. A client that fetches directly for its user is not automatically the same use as a hosted data relay, but neither is cleared here. No general legal entitlement, geographical availability or SLA is asserted. Do not work around geographical restrictions, access denial or rate limits. Keep offline/manual operation available.

Apply the existing transport proposal: verified HTTPS only, reviewed exact origins/paths, redirects disabled, no cookies/authentication/proxies with ambient credentials, bounded DNS/connect/total deadlines, cancellation, one in-flight request per feed, bounded JSON/body/header/nesting/array sizes and bounded retries. Enforce bounds before parsing/allocation. An initial 1 MiB body cap and 1,000-row Gate/720-row Kraken candle caps are reasonable fixture targets, not proof of all library allocation bounds. Preserve good old observations as **stale**, never zero. Failures must not interrupt mining or change pool/donation routing.

Suggested initial *local* freshness policy for fixture work: refresh latest references every 60 seconds while displayed, candles once per completed minute, and expire observations after 120 seconds with at most 30 seconds cross-feed observation skew. These are configurable product proposals, not measured provider guarantees; source-event-age requirements remain unmet by ticker-only data. Economic evaluation requests/reuses fresh data at its independent evaluation time and cannot treat the entire 8h history as an 8h cache lifetime.

## 7. Kryptex actual-credit capability assessment

### What is actually published

The authoritative public API page links **`https://pool.kryptex.com/openapi.yaml`**, OpenAPI **3.0.3**, API version **`1`**. The exact downloaded schema reviewed had SHA-256 **`70072837d1f7120ae1fdffb289b8ce0716bf4d567cbd4ddc2cba3128186ce3db`**; this identifies the research snapshot, not a stable version guarantee.[^kryptex-api][^kryptex-schema]

The following are **documentation path templates only; none was requested**. `{coin}` is a generic schema path parameter; the Pearl website uses the `prl` route. The schema does not separately promise identical support/semantics for each coin.

| Published path / schema | Documented shape | Can it establish this miner's actual PRL credits? |
| --- | --- | --- |
| `/{coin}/api/v1/miner/reward-chart/{address}` | Summary **“31-day reward chart”**; array of `RewardPoint { date: integer, reward: number }`. No worker argument or pagination/time-range parameters. | **No.** At most an address-scoped dated reward aggregate candidate. Missing stable credit identity/revision, credit status/time basis, exact units/scale, bucket boundaries/timezone, coverage/finality and worker attribution. |
| `.../miner/chart/{address}/{worker}` and `.../{worker}/{scheme}` | `WorkerChartPoint { timestamp, hashrate, shares }`; optional `days`; scheme enum `solo`, `pps`, `prop`. | Operational work history, **not a token-credit ledger**. A worker parameter does not imply worker reward records exist. |
| `/{coin}/api/v3/miner/workers/{address}` | `PaginatedWorkers { count, results }`; `Worker { worker, scheme, hashrate, shares, last_share }`. | Worker status and activity, not credited token amounts; visible workers do not prove account exclusivity or historical membership. |
| `.../miner/balance/{address}` | `Balance { total, threshold, reached_pct, unconfirmed, confirmed }`. | **No.** Balance and pending/confirmed stocks are not production flows. |
| `.../miner/payouts/{address}`, `/stats`, `/csv` | `Payout { date, received, txid, status }`; paginated history has `count/results`; stats include `paid`, `unpaid`, `reward.week/month`. | **No.** Transaction IDs identify payouts, not individual earning credits; moving earned funds is not new production. Weekly/monthly aggregates have no per-credit identity/attribution contract. |
| `.../miner/blocks/{address}` | `MinerBlock` includes block `hash`, `height`, `reward`, `date`, `status`, plus `worker`, `scheme`, `miner_effort`; paginated. | **No general credit substitute.** A found block or chain reward is not a documented final pool credit to this worker. No stable credit revision/reversal or PPS+ attribution contract is supplied. |

The paths and fields above are direct schema facts.[^kryptex-schema] The insufficiency conclusions follow from this project's actual-income contract, not a claim that Kryptex internally lacks a ledger.

### Stable identity, revisions and attribution

* `RewardPoint` has no `id`, `revision`, reversal reference, finalization watermark or credit status. A local hash of `(date,reward)` is **not** a provider credit ID: editing a day's value would create a different hash and can double-count it. Using `(address,date)` as a local bucket key can store aggregate snapshots, but cannot manufacture immutable credit facts or an upstream revision contract.
* The reward chart is addressed by `{address}`, not `{worker}`. Do not prorate its rewards by worker shares/hashrate and call that actual local income. Difficulty, timing, fees, credit delays and historical workers make that an estimate at best.
* A single visible worker does not establish an exclusive account. Even an explicit local exclusive-account declaration only changes disclosed scope; it cannot fix absent credit IDs/time/finality. Keep wallet/account-scope aggregates separate from worker facts.
* The schema marks reward amounts as JSON `number`, without an atomic-unit scale or precision promise. Preserve decimal lexemes if such aggregates are ever supported; do not infer Pearl's atomic precision or fee-net/gross semantics from display output. Missing required semantics is a capability failure, not zero income.

### Exact rolling 24h versus calendar/day buckets

The title **“31-day reward chart”** does not specify `date` units, whether points are daily or another resolution, the bucket timezone, open/closed bounds, whether today's point is partial, earning time versus booking time, or whether revisions can occur after a day closes. Those meanings remain **unverified**; this note deliberately does not claim the schema proves UTC daily credits.[^kryptex-schema]

Even if later documentation confirms complete calendar-day buckets, they generally cannot answer `N-24h < credit_time <= N` at an arbitrary instant. Summing two whole days, allocating a daily total uniformly, or differencing periodically observed cumulative/balance numbers cannot reconstruct exact boundary-hour credits. A declared bucket-resolution approximation must not be labelled exact rolling 24h. “Today” additionally depends on the selected reporting timezone and may be 23/24/25 elapsed hours.

Likewise, a 31-day chart is not all-time history. Persisting observations does not prove completeness before collection or across gaps; a disappearing point may be retention expiry rather than a reversal. Late discovery retains its source earning/credit interval, never the discovery time. An empty chart or HTTP failure is not evidence of a complete zero-income interval.

**Pool versus account mining:** Kryptex explicitly supports mining to a coin wallet or to a Kryptex account; its account-mining guide says earnings are credited/stored in **Bitcoin**, including when mining supported PRL. Do not assume account-converted BTC or a USDT withdrawal is a PRL-credit ledger, infer a sale, or fetch account data as a fallback.[^kryptex-prl][^kryptex-account]

**Safe capability result:** `stable_credit_records=unsupported_by_reviewed_schema`, `worker_credit_attribution=unsupported_by_reviewed_schema`, `exact_rolling24h=unverified/unsupported_for_actuals`, `complete_cumulative=unverified`. This describes the published evidence, not a universal assertion that no other API can exist. A future address-scoped aggregate adapter should be named accordingly and remain unavailable until its bucket/unit/terms contract is established.

### Minimum contract needed to unblock actuals

Require documented token/network identity and scale; stable provider credit IDs and revision ordering; signed corrections/reversals with replacement-versus-delta semantics; worker/generation or clearly disclosed wallet/account attribution; earning/booking time and exact interval bounds; posted/final/pending distinction; fee/net basis; pagination/cursor consistency; coverage start/end and finality watermarks; retention and late-update guarantees; cache/rate/access terms. Obtain these through public documentation or a future authorized vendor clarification, not speculative wallet probes. No support request was sent during this research.

## 8. Synthetic schema fixtures

**Everything in this section is invented for offline tests. None is a sampled market quote, provider revenue, wallet, worker or credit.** `1800000000` is a synthetic injected minute boundary, not a historical-price claim. Examples show selected required fields, not full vendor responses.

Gate proposed eight-position REST candle dialect:

```json
[["1799999940", "5.00", "2.50", "2.60", "2.40", "2.45", "2.00", "true"]]
```

Gate latest-reference subset:

```json
[{"currency_pair":"PEARL_USDT","last":"2.50","highest_bid":"2.49","lowest_ask":"2.51"}]
```

Kraken OHLC fixture includes a terminal uncommitted row that must not enter an average:

```json
{"error":[],"result":{"USDTZUSD":[
  [1799999940,"0.97","1.00","0.96","0.98","0.98","10.00",3],
  [1800000000,"0.98","1.00","0.97","0.99","0.99","1.00",1]
],"last":1800000000}}
```

Kraken latest-reference subset:

```json
{"error":[],"result":{"USDTZUSD":{"a":["0.99","1","1.00"],"b":["0.97","1","1.00"],"c":["0.98","1.00"]}}}
```

Kryptex **aggregate-only** schema fixture; this is deliberately insufficient to create credits:

```json
[{"date":1800000000,"reward":4.25}]
```

For generic future-ledger tests only, use an internal synthetic record such as `(provider=fixture, scope=synthetic-worker-A, credit_id=credit-A, revision=1, credited_at=T, amount="4.25", asset=Pearl-mainnet-native)`. **This is not a Kryptex schema or a claim that those fields are available from Kryptex.**

## 9. Deterministic acceptance cases

These are proposed offline tests, **not executed adapter results**. Inject clocks and HTTP responses; no test requires internet, a wallet, account, pool connection or GPU. Unless varied, use `N=1800000010`, `g=5`, hence `E=1800000000`.

| ID | Fixture and required assertion |
| --- | --- |
| M01 | Registry maps only the reviewed Pearl-mainnet identity to `PEARL_USDT`. A different network, wrapped asset, `PRL_USDT`, wrong quote or metadata mismatch fails; ticker text alone cannot authorize it. |
| M02 | Generate exactly 60 closed Gate rows at `E-3600+60*i`, `i=0..59`, with close `2.00`. Mean is exactly `2.00` USDT/PEARL, coverage is 60/60, end E. Repeat with nonconstant closes to detect selecting the last close instead of computing the mean. |
| M03 | Remove one minute, add an extra unrelated old minute, and retain 60 rows overall. Coverage fails despite row count 60. A duplicate key inside one response also fails; no fabricated zero or forward-fill. |
| M04 | Mark one in-window Gate row `"false"`; return a closed row at E too. The former invalidates complete-window coverage; the latter is outside `[S,E)` and cannot replace it. |
| M05 | Supply the seven-position Gate documentation example shape, a six-position legacy shape, a numeric position 7, and string `"TRUE"`. All fail the proposed strict dialect; position 6 is never guessed as closure. A JSON Boolean requires a separately enabled dialect, not accidental coercion. |
| M06 | Reorder valid Gate rows: the resulting sorted mean is unchanged. Supply a millisecond timestamp, fractional minute timestamp, non-minute start or future interval: reject it. No automatic divide-by-1000 “repair.” |
| M07 | Build two authorized Gate slices with identical boundary overlap: merge once. Conflicting overlapping OHLC or closure values invalidate the snapshot. An unchanged/full page cannot cause an unbounded pagination loop. |
| M08 | Gate planner emits explicit `interval=1m`, no `limit` with `from/to`, and no `page/offset`. Each request remains below 1,000 candidate rows, total requests are capped, surplus boundary rows filtered, and missing boundary data remains partial. |
| M09 | Parse Kraken close from index 4, not Gate index 2. Exclude the original terminal Kraken row even with an old-looking timestamp or a fast client clock. A malformed row/order contract fails rather than silently moving the exclusion after sorting. |
| M10 | Kraken response contains `USDTZUSD` and `last`, or an unexpected extra market. Select only the metadata-authorized market; reject ambiguity. `error:["EAPI:Rate limit exceeded"]` with HTTP 200 publishes no good quote. |
| M11 | Kraken returns 720 rows including one uncommitted row: at most 719 are eligible. An earlier `since` does not prove older coverage. Missing 480-minute keys fails the 8h window; asking for a 24h minute window without local history yields unsupported/partial, not fabricated pagination. |
| M12 | With identical Kraken cursor/rows on retry, storage is unchanged and cursor polling waits. A failed parse does not advance the committed cursor. A Trades cursor cannot be supplied to the OHLC state machine. |
| M13 | Gate latest `2.50` USDT and Kraken latest `0.98` USD/USDT yield exactly `2.45` USD/PEARL. FX inversion, selecting volume instead of price, USDC substitution and automatic parity must all fail unit checks. |
| M14 | Synthetic aligned minute series alternates `(P,F)=(1,1),(3,3)`. Mean product is `5`, product of means is `4`; historical USD output must use the former. Missing one FX minute leaves historical USD unavailable while complete USDT history remains valid. |
| M15 | Ticker received now has no source event time: observation is fresh, event age remains unavailable. Repeated polling cannot set event age to zero. An event-age-required economic policy is ineligible until a timestamp-bearing definition is qualified. |
| M16 | Age values at, just below and just above configured expiry/skew thresholds produce deterministic status. Missing/stale FX preserves valid USDT output but disables USD and USD-dependent decisions. Wall-clock jumps cannot reset monotonic request deadlines. |
| M17 | Set display to 60 minutes and economics to 480 minutes; verify separate keys, coverage and results. Changing economics to another supported duration or interval leaves display unchanged. Explicit start triggers evaluation once; hourly evaluation, 60-second refresh and telemetry remain independent; stop cancels all pending evaluation/retry work. |
| M18 | Zero/negative/NaN/infinite/overflow prices, invalid decimal syntax, high below low, close outside OHLC bounds, negative volume and crossed optional bid/ask fail validation. Valid zero volume alone is not an invented missing price; negative credit corrections are handled separately from market prices. |
| M19 | HTTP 429/503, timeout, oversized body/array, redirect, malformed/trailing JSON and certificate failure hit finite retry/size budgets. No auth/cookies or alternate insecure origin is used; stale cache retains old as-of; mining threads are unaffected. |
| C01 | Replay `RewardPoint {date,reward}` and worker-status responses: no generic credit records are generated, even when exactly one worker is visible. A locally computed hash/date key does not satisfy provider ID/revision requirements. |
| C02 | Increase confirmed balance or return a payout with `txid`; credited production remains unavailable/unchanged. Add a found block with worker attribution: it still cannot be relabelled as a posted pool credit. |
| C03 | Supply two hypothetical complete day totals, 8 and 16 tokens, then ask for rolling 24h at midday. Exact rolling income remains unavailable; returning 24 or a uniformly allocated 12 is forbidden without sub-day credit facts. A 31-day chart also cannot establish lifetime income. |
| C04 | Generic synthetic ledger only: repeat `(credit-A,revision=1,amount=4.25)` across retries/pages, then replace with revision 2 amount `3.75`. Current total becomes `3.75`, not `8.00`. Replayed old revision does nothing; same revision with conflicting content is invalid. Test a signed reversal under the declared delta/replacement contract. |
| C05 | Generic ledger only: with current time T, credit at `T-86400` is excluded, `T-86400+1` included, T included, `T+1` invalid/not yet eligible. Discover an older credit now: preserve its source time, not discovery time. Attribution mismatch never joins local-worker totals. |
| C06 | Generic ledger only: valid complete zero-income coverage produces zero; empty response with unknown coverage produces unavailable. Wallet exclusivity must be explicitly configured/disclosed, and cannot supply missing IDs/finality. Retention expiry is not a reversal. |
| C07 | Generic ledger only: latest market changes revalue unchanged actual token totals. Previously sold/withdrawn status remains unknown. Reporting-zone DST tests keep calendar today separate from the rolling 24h interval and aligned forecast inputs. |

## 10. Explicit release gates and remaining gaps

* **Gate candle dialect and time:** settle the seven/eight-field contradiction, precise row timestamp anchor, bound inclusion, row ordering, sparse/no-trade behavior, closure latency and revision policy. Until then, the proposed strict parser/window design is fixture-ready, not live-qualified.
* **Latest-quote event freshness:** both recommended ticker schemas lack last-trade timestamps. Observation-only latest references are publishable with that caveat; an event-age-based optimizer needs a separately qualified timestamp-bearing price definition. Gateway time is not trade time.
* **FX history:** recent 1m coverage can support 1h and 8h but is finite. No full historical archive, guaranteed dense coverage, cross-venue simultaneity or retrieval performance was tested. No live prices/candles were fetched.
* **Identity:** the Pearl mapping is first-party documentation plus public instrument metadata, not chain-genesis/custody verification. Token atomic precision and an exchange quote's cross-chain equivalence were not established.
* **Credits:** reviewed Kryptex schema cannot meet stable/revisable worker-attributable PRL-credit facts, exact rolling 24h or complete cumulative actual income. Bucket/time/scale/finality semantics remain unverified. No address endpoint, account export or support channel was used to fill these gaps.
* **Terms and service behavior:** commercial/local display/caching/derived-data rights and redistribution remain a release review, not implied by no-auth access. No provider availability, rate-header behavior, cache behavior or data SLA was live-tested.
* **Implementation:** no adapter, ledger, transport, configuration, packaging or runtime setting is changed by this note. The tests above are specifications, not passing results. Missing capabilities must stay visible as partial/unavailable/unsupported rather than zero or estimated “actuals.”

## Sources

All links below are first-party sources reviewed on the evidence date. Endpoint names and schema JSON pointers identify the exact contract being cited; live documentation can change. Instrument-metadata citations support selected identity fields only, not live financial figures or an access entitlement.

[^gate-listing]: Gate, [Gate to List Pearl (PEARL) for Spot and Convert Trading](https://www.gate.com/announcements/article/102410), 2026-10-23, “Listing Schedule” and “Overview”: `PEARL/USDT`, opening time, Pearl Research website and mainnet explorer identity links.
[^pearl-network]: Pearl Research Labs, [Network — The Pearl Protocol](https://pearlresearch.ai/network), native-currency description, network repository link and PRL mining reference.
[^gate-meta]: Gate public instrument metadata: [`/api/v4/spot/currency_pairs/PEARL_USDT`](https://api.gateio.ws/api/v4/spot/currency_pairs/PEARL_USDT) and [`/api/v4/spot/currencies/PEARL`](https://api.gateio.ws/api/v4/spot/currencies/PEARL), selected identity fields read without authentication; field definitions in [Spot API](https://www.gate.com/docs/developers/apiv4/en/spot), “Query single currency information” / “Query single currency pair details.”
[^gate-spot]: Gate international [Spot API v4](https://www.gate.com/docs/developers/apiv4/en/spot), `GET /spot/candlesticks` (“Market K-line chart,” parameters, response schema and inconsistent example); `GET /spot/tickers` and `Ticker` schema; `GET /spot/trades` and `Trade` schema; `GET /spot/order_book` response timestamps. These endpoint-specific contracts are not interchangeable with futures, regional or WebSocket schemas.
[^gate-general]: Gate international [API v4 general documentation](https://www.gate.com/docs/developers/apiv4/en/), “Access URL,” “Time,” “API Gateway in/out time,” general pagination discussion, and [Frequency limit rule](https://www.gate.com/docs/developers/apiv4/en/#frequency-limit-rule).
[^gate-ws]: Gate, [Spot WebSocket v4 — Candlesticks Channel](https://www.gate.com/docs/developers/apiv4/ws/en/), `spot.candlesticks` notification fields `t`, `a`, `w`; evidence of a different schema, not authorization to infer REST closure.
[^gate-rate-change]: Gate, [Latest Announcement on API Rate Limit Adjustments](https://www.gate.com/announcements/article/33995), 2024-01-14, effective 2024-01-22; public endpoints changed from shared 900r/s to 200r/10s per endpoint/IP.
[^gate-terms]: Gate, [User Agreement](https://www.gate.com/legal/user-agreement), API scope in the introduction; §§1.3(h), 10.1(d), 12.6, 16.1–16.2 and 17.1. This note identifies review obligations and does not offer a legal determination.
[^kraken-pairs]: Kraken, [Get Tradable Asset Pairs](https://docs.kraken.com/api-reference/market-data/get-tradable-asset-pairs), `GET /public/AssetPairs`, `pair`, `assetVersion`, `base`, `quote`, `altname`, `wsname`; [API symbols and tickers](https://support.kraken.com/articles/360000920306-api-symbols-and-tickers), internal versus common names.
[^kraken-meta]: Kraken public instrument metadata, [`/0/public/AssetPairs?pair=USDTUSD`](https://api.kraken.com/0/public/AssetPairs?pair=USDTUSD), selected identity mapping read without authentication: `USDTZUSD`, `USDTUSD`, `USDT/USD`, USDT base / ZUSD quote.
[^kraken-public]: Kraken, [Public endpoint examples](https://support.kraken.com/articles/360000919986-public-endpoint-examples-you-can-try-them-directly-in-a-web-browser-), public market-data access without a Kraken account. Documentation examples are not live observations for this note.
[^kraken-ticker]: Kraken, [Get Ticker Information](https://docs.kraken.com/api-reference/market-data/get-ticker-information), and its [published Markdown/OpenAPI rendering](https://docs.kraken.com/api-reference/market-data/get-ticker-information.md), `GET /public/Ticker`, `components.schemas.ticker` (`AssetTickerInfo`): `a`, `b`, `c`, `p`, `t`, `o`, no event-time field; operation `security: []`.
[^kraken-ohlc]: Kraken, [Get OHLC Data](https://docs.kraken.com/api-reference/market-data/get-ohlc-data), and its [published Markdown/OpenAPI rendering](https://docs.kraken.com/api-reference/market-data/get-ohlc-data.md), `GET /public/OHLC`: terminal uncommitted entry, 720-recent-entry hard depth, `interval`, `since`; `components.schemas.tickData` eight-entry shape and `components.schemas.ohlc.result.last` committed-data cursor; operation `security: []`.
[^kraken-history]: Kraken, [Historical data](https://docs.kraken.com/exchange/guides/general/historical-data), public OHLC/Trades contracts, Unix time conventions, forward polling cursor use and recent-depth limits. Its general collection advice does not override the OHLC endpoint's explicit historical-depth limitation.
[^kraken-trades]: Kraken, [Get Recent Trades — published Markdown/OpenAPI](https://docs.kraken.com/api-reference/market-data/get-recent-trades.md), `GET /public/Trades`, count bounds, trade row fields and cursor; no request to the live endpoint was made.
[^kraken-limits]: Kraken Support, [What are the API rate limits?](https://support.kraken.com/articles/206548367-what-are-the-api-rate-limits-), updated 2026-10-01, “Public (REST Market Data)”: IP/pair dimensions, one-per-second guidance and extended throttling after continued requests.
[^kraken-terms]: Kraken, [Global Terms of Service](https://www.kraken.com/legal/global-terms), “Our Content” and “Restrictions”: own-benefit limited permission, third-party distribution/commercial exploitation and extraction restrictions. Applicable regional/product terms require a separate release review.
[^kryptex-api]: Kryptex, [Kryptex Pool API](https://pool.kryptex.com/api), public API description, base origin, endpoint catalogue and official OpenAPI download link.
[^kryptex-schema]: Kryptex, [`openapi.yaml`](https://pool.kryptex.com/openapi.yaml), OpenAPI 3.0.3 / API version `1`; `paths` for reward-chart, miner/worker charts, balances, payouts, miner blocks and v3 workers; `components.schemas.RewardPoint`, `WorkerChartPoint`, `Worker`, `Balance`, `Payout`, `PayoutStats`, `MinerBlock`, `PaginatedWorkers`; `components.parameters.Days`, `Page`, `Scheme`. Reward chart has no worker, cursor, revision, credit ID or explicit bucket/unit/finality fields. SHA-256 snapshot identifier is recorded in §7.
[^kryptex-prl]: Kryptex, [Pearl Mining Pool](https://pool.kryptex.com/prl), PRL/PearlHash identity, wallet/worker versus username/worker modes, and linked Pearl Research Labs desktop wallet. Dynamic pool statistics and financial figures are not reproduced or used.
[^kryptex-account]: Kryptex, [Mine Anything — Get Bitcoin!](https://pool.kryptex.com/articles/account-mining-en), account-mining description and important information: earnings credited/stored in BTC; supported-coin table includes PRL. This is documentation evidence only; no account data was accessed.
