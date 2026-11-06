# Gate / Kraken REST market-source qualification recheck

**Evidence reviewed:** 2026-11-06. **Outcome:** documentation research only; **do not enable production candlestick admission or public polling on this evidence**.

This supplements [market and credit provider research](market-and-credit-providers.md), [the implemented market boundary](../market-data.md) and P-06 in the [execution plan](../architecture/execution-plan.md). It does not change their implementation status or qualify income, trading, forecasts or mining controls.

## 1. Decision and research boundary

Neither outstanding REST timestamp anchor was settled by the reviewed authoritative contracts. Gate's seven/eight-position contradiction is also still present **in its published OpenAPI source**, not merely in a stale documentation rendering. The additional source finding is that Gate's machine schema permits rows with **at least six** string elements, without an eight-element maximum; that is not a machine-enforced eight-position contract.[^gate-spec][^gate-spot][^kraken-spec][^kraken-ohlc]

| Question | Established on this review | Admission decision |
| --- | --- | --- |
| Gate REST candle layout | Eight meanings in prose; seven strings in its example, ending in `"true"`; structural schema has `minItems: 6`, string items and no `maxItems`.[^gate-spec] | **Unqualified.** Do not guess a seven-position dialect or promote the proposed eight-position fixture dialect to production. |
| Gate REST row timestamp | Described only as “Unix timestamp with second precision”; request `from`/`to` describe query bounds, not the meaning of the returned row's timestamp.[^gate-spec][^gate-spot] | **Unqualified open/close anchor.** No justified mapping to `[t,t+60)`. |
| Kraken REST row timestamp | Eight-element row begins with `int <time>`; the endpoint and raw schema do not say interval start or interval end. Its first-party historical guide also repeats only `time`.[^kraken-spec][^kraken-ohlc][^kraken-history] | **Unqualified open/close anchor.** Excluding the current row does not identify the other rows' boundaries. |
| Kraken REST closure/depth | Original terminal row is always current/not committed, regardless of `since`; at most 720 recent entries, with older entries not retrievable through this endpoint.[^kraken-ohlc] | These contract facts are usable, but **do not remove the timestamp gate**. At most 719 entries in one response are eligible after terminal-row exclusion. |
| Latest ticker freshness | Gate `last` and Kraken `c[0]` identify last-trade price; neither reviewed ticker schema supplies its trade-event time.[^gate-ticker][^kraken-ticker] | Observation-age-only reference is a technically supported definition, **not trade-age qualification or permission to activate a feed**. |
| Public access / reuse | The market operations explicitly need no authentication, but applicable terms restrict access/use and do not supply an unrestricted redistribution grant.[^gate-spec][^kraken-ohlc][^kraken-ticker][^gate-terms][^kraken-terms] | Keep use-rights/regional/deployment approval separate from schema qualification; public does not mean unrestricted. |

**Scope actually exercised:** Context7 resolution and documentation queries **first**, then direct reads of official REST pages, raw specifications, publisher-owned SDK source and first-party guides/terms. Context7 and search results were discovery aids, not evidence in place of the linked source. No ticker, candle, trade, order-book or instrument-metadata API requests were made. No wallet/account/private endpoints, credentials, login, installation, support request or outbound submission were used. No miner, game, GPU or build was started. Only this research note was authored; no implementation, tests, runtime settings, execution-plan edit, staging or commit is supplied.

“Unqualified” below means **not established by this bounded source review**, not that a vendor could never document the property. No sampled market behavior, third-party wrapper, generated summary, WebSocket field or timestamp magnitude is used to manufacture a REST guarantee.

## 2. Gate: the source does not repair the example

### Exact authoritative contract

The international API documentation identifies `https://api.gateio.ws/api/v4`, links the official SDK repositories, and says SDKs and documentation are generated from the same OpenAPI specification. Its current heading/changelog is **v4.106.179**, dated **2026-11-03**. Consequently, agreement among generated clients is not independent confirmation of an ambiguous underlying description.[^gate-general]

The official Go SDK publishes `openapi.yaml`. At pinned commit `9df90626fca7d38e6d2e443efcb938807a4f7c44`, inspect `paths["/spot/candlesticks"].get.responses[200].content["application/json"]`, especially `schema.items` and `example` (lines 29337–29447). The response is an outer array whose inner schema contains:[^gate-spec]

```yaml
type: array
minItems: 6
items:
  type: string
```

There is **no inner `maxItems`, positional tuple schema or closure enum** there. Separately, its inner-array description enumerates these eight meanings, in order:[^gate-spec]

1. Unix timestamp with second precision.
2. Quote-currency trading volume.
3. Closing price.
4. Highest price.
5. Lowest price.
6. Opening price.
7. Base-currency trading volume.
8. Whether the window is closed: true means complete, false means not yet complete.

The same operation's example has **seven** elements and ends with the string `"true"`; it does not contain a separate eighth element. The current REST page repeats this mismatch. This note deliberately does not reproduce documentation price/volume samples as market observations.[^gate-spec][^gate-spot]

**What follows, and what does not:** the intended eight-field prose mapping is documented; an exact-eight runtime shape is not consistently specified. The structural schema can admit shorter/longer string arrays, but it does **not** define their meanings. Neither “the example must have omitted base volume” nor “the service must always send eight” is established. Accepting six/seven fields because they pass generic OpenAPI validation would abandon the application's complete-window/closure requirements. Requiring eight strings with exact `"true"`/`"false"` remains a conservative **project fixture rule**, not newly verified provider behavior.[^gate-spec][^gate-spot]

The official Python SDK at pinned commit `6ce527946f500060c41f9eeebade4835487baef8` returns `list[list[str]]` from `list_candlesticks`, sends `GET /spot/candlesticks` without authentication, and describes `_from`/`to` as request bounds. It does not add a fixed row-length, closure-position or returned-time-anchor definition. A generic deserializer is not server implementation evidence.[^gate-python]

### Timestamp and related gaps

The returned timestamp's entire definition is **“Unix timestamp with second precision.”** “Start time of candlesticks” belongs to the **`from` parameter**, while `to` is the query end. Neither says that **position 0 of each returned row** is the candle's opening boundary. Minute divisibility cannot distinguish a minute start from a minute end; both can be divisible by 60.[^gate-spec][^gate-spot]

The specific REST operation does not establish inclusive/exclusive query bounds, response ordering, dense no-trade-minute bars, closure-publication delay, closed-bar immutability/revision ordering or an exhaustive retention guarantee. These remain operational unknowns, not promises borrowed from the general pagination section. The documented ceiling is 1,000 points; `limit` conflicts with `from`/`to`, and `interval=1m` is supported.[^gate-spec][^gate-general]

The WebSocket candle object separately has named base-volume `a` and Boolean closure `w`; its table even warns that `true` may be missing. That is a **different transport and schema**, not a correction to the REST array, a REST Boolean dialect or a missing-flag acceptance rule.[^gate-ws]

**Minimum missing evidence:** a coherent, applicable **international Spot REST** positional contract, including whether fields are optional and their closure types/positions, plus an explicit returned-timestamp-to-interval definition. No vendor clarification was solicited.

## 3. Kraken: closure is explicit; the REST time anchor is not

The current raw Spot REST OpenAPI is version **1.1.0**. `components.schemas.tickData` explicitly gives:

```text
[int <time>, string <open>, string <high>, string <low>,
 string <close>, string <vwap>, string <volume>, int <count>]
```

Its row schema has `minItems: 8` and `maxItems: 8`; individual item types are expressed as a string/integer union, while the description supplies position meanings. `paths["/public/OHLC"].get` defines `interval` in minutes and describes `since` as a timestamp for incremental updates; `components.schemas.ohlc...last` is the ID for polling new committed data. **None of these definitions calls returned `time` an opening or closing boundary.**[^kraken-spec][^kraken-ohlc]

The original terminal entry must be excluded because Kraken expressly identifies it as the current, not-yet-committed timeframe, always present regardless of `since`. The client must retain that positional identity before any sorting. The 720-entry recent-depth limit is also express; repeatedly supplying an old `since` cannot recover unlimited history. The derived 719-complete-entry ceiling is an upper bound, **not proof of 719 consecutive covered minutes**.[^kraken-ohlc]

The official historical-data guide identifies `since` as Unix time but labels each returned row's first field only `time`. The official Go SDK's `OHLC` method returns `Response[map[string]any]` and supplies no typed candle/time-boundary model. These sources do not close the REST anchor gap.[^kraken-history][^kraken-go]

### Historical CSV guide: useful but not a REST anchor guarantee

Kraken's OHLCVT download guide, updated **2026-11-02**, links the REST OHLC endpoint, describes downloadable CSV rows as `timestamp, open, high, low, close, volume, trades`, calls their timestamps UNIX/Epoch/POSIX time, and says intervals without trades are omitted. Its “About the data” says the same data is available programmatically through the public REST API. However, it does **not** state whether the timestamp labels interval start or end. CSV OHLCVT also lacks REST's VWAP field and terminal-current-row contract; it is not an interchangeable parser dialect.[^kraken-download]

This first-party sparse-data warning defeats any general assumption of dense minute coverage. It does not justify synthesizing REST candles or asserting every empty REST interval has one uniquely diagnosed cause. Missing requested minutes must remain partial/unavailable under this project's strict coverage policy.[^kraken-download][^kraken-history]

There is also a source inconsistency worth preserving: the developer historical guide says Kraken does not provide a bulk historical data dump, while the current support download page offers OHLCVT archives. The endpoint-specific recent-depth restriction still controls REST; archives are a **separate future qualification task**, not a pagination workaround. No archive or manifest was downloaded or treated as evidence of price history in this review.[^kraken-history][^kraken-download][^kraken-ohlc]

### Why WebSocket documentation cannot decide this

Kraken WebSocket v1 OHLC explicitly separates `epoc_last` (candle last update) from `epoc_end` (interval end). WebSocket v2 explicitly defines `interval_begin` as interval start. These demonstrate different published timestamp roles, not a REST equivalence rule. Neither contract states that Spot REST row position 0 is the same field. No opening-boundary guarantee is imported into REST from either version.[^kraken-ws1][^kraken-ws2][^kraken-ohlc]

**Minimum missing evidence:** an explicit first-party definition tying **Spot REST `/public/OHLC` row `time`** to the beginning/end of the requested interval, including the interval-boundary convention needed for deterministic minute alignment. The terminal-row rule and Unix terminology alone are insufficient.

## 4. Latest reference: two ages, not one

| Source | Price and timestamp evidence | Honest output |
| --- | --- | --- |
| Gate Spot REST ticker | `last` is last trading price; bid/ask are distinct fields. `Ticker` has no general last-trade timestamp. `etf_pre_timestamp` means ETF previous rebalancing time.[^gate-ticker] | “Last-trade reference, observed at …; trade time not supplied.” |
| Gate gateway | `X-In-Time` is gateway request receipt; `X-Out-Time` is gateway response return, in microseconds.[^gate-general] | Request/transport timing only, not price-event timing. |
| Kraken Spot REST ticker | `c` is `[price, lot volume]` for the last trade; `t` is trade counts, `o` today's opening price. The ticker schema has no last-trade event-time field.[^kraken-ticker] | The same observation-only caveat; neither `c[1]` nor `t` is a timestamp. |

**Project definitions:** `observation_age = evaluation_time - locally_recorded_receipt_time`; `trade_event_age = evaluation_time - validated_source_trade_time`. Retain request/receipt clocks and source/generation separately. The second quantity is **unavailable** for these ticker-only contracts; a fresh response, unchanged price, HTTP date, gateway clock or polling cursor cannot make it zero.[^gate-ticker][^gate-general][^kraken-ticker]

For an entirely synthetic clock test, receipt at injected second 115 and evaluation at 120 yield observation age 5 seconds, while absent trade time still yields **unavailable trade age**. A policy requiring recent trade events must therefore refuse ticker-only eligibility even when observation-age/skew checks pass. Neither latest reference is an executable bid, a guaranteed sale price, a minute close or a USDT/USD parity assertion.[^gate-ticker][^kraken-ticker]

**Possible separately scoped upgrade, not activation:** public Gate `/spot/trades` documents `create_time`, millisecond-precision `create_time_ms`, price, pair and IDs; Kraken `/public/Trades` documents price/volume/time/trade-ID rows and a separate polling cursor. Kraken's first-party historical guide calls Trades time-ordered. These are candidates for a timestamp-bearing **observed trade** basis, not fields that can be grafted onto an unrelated ticker. Newest-selection/completeness, tie/order handling, units/precision, identity, correction behavior, request budgets and use rights need their own admission rules. No claim that `limit=1`/`count=1` necessarily establishes a globally latest trade, and no trade request, is made here.[^gate-trades][^kraken-trades][^kraken-history]

## 5. Technical public access is not data-use clearance

The reviewed Gate Spot operations and Kraken OHLC/Ticker/Trades operations explicitly require no authentication (`security: []` in the specifications). They do not technically require a wallet, account export or API key. **This is an endpoint authentication fact, not a determination that this application's use is contractually authorized in every location.**[^gate-spec][^gate-python][^kraken-ohlc][^kraken-ticker][^kraken-trades]

* **Gate:** the current User Agreement expressly includes APIs in the services. Section 1.3(h) restricts non-designated/undocumented interfaces and evasion of access controls; §§2.4–2.6 impose location/eligibility restrictions; §10.1(d) restricts resale/commercial use unless expressly agreed in writing; §16.2 grants a limited personal/non-commercial IP license. Section 12.6 provides services/information as-is/as-available without accuracy or timeliness commitments.[^gate-terms]
* **Kraken:** Global Terms, last updated **2026-10-30**, §8 permits Our Content only for one's own benefit under a revocable permission. Section 9 restricts third-party availability/commercial exploitation, extraction, automation and development of interacting third-party applications without prior written consent; it also prohibits accessing services unavailable in one's location. Section 11 disclaims accuracy/availability and supplies no uptime guarantee. The terms identify separate regional terms for Brazil, Canada and the EEA; no applicable regional clearance is inferred here.[^kraken-terms]
* **Licenses are different objects:** Gate's OpenAPI metadata names Apache 2.0 but **also** points `info.termsOfService` to an agreement PDF. A software/specification license does not establish a license to redistribute live market responses. The linked PDF could not be text-reviewed with the available document readers in this pass; its contents are not relied on, and potentially additional applicable API terms remain an explicit review gap. No software was installed to extract it.[^gate-spec][^gate-api-terms]

**Release recommendation, not legal advice:** separately review direct local display, caching/retention, derived averages, commercial use, attribution, exports, bundled sample data and hosted relay/redistribution for each provider and applicable region. Direct fetching by an end user's client is not the same architecture as a central relay, but neither is automatically cleared by an unauthenticated endpoint or an open-source SDK. Do not use website scraping, alternate regional hosts, proxies or IP sharding to bypass refusal. No broad data-redistribution right or universal access entitlement was established.[^gate-terms][^kraken-terms]

For an eventually authorized adapter, Gate's current public-endpoint rule is **200 requests / 10 seconds / endpoint / IP**. Kraken Support recommends **one public request per second or less**, with IP+pair limits for OHLC/Trades and IP limits for other public endpoints; continuing while throttled can prolong restriction. Preserve conservative shared budgets and bounded backoff. These are published limits/guidance, not measured latency, uptime, closure delay or an invitation to maximize traffic.[^gate-general][^kraken-limits]

## 6. Exact remaining gates and a useful next slice

### Admission gates that remain closed

1. **Gate candle schema:** reconcile the seven-field example, eight-field prose and minimum-six machine schema; establish supported shapes, positions and closure encodings.[^gate-spec]
2. **Both REST time anchors:** obtain explicit row-to-interval definitions; do not infer minute-open or subtract one minute speculatively.[^gate-spec][^kraken-spec][^kraken-ohlc]
3. **Source use / deployment:** applicable access, local/commercial display, retention/derived/export/redistribution rights and regional terms remain unapproved; this note does not qualify the existing HTTPS helper's deployment/trust boundaries.[^gate-terms][^kraken-terms] The latter boundaries remain in [public HTTPS evidence](../public-https.md).
4. **Event-age policy, if required:** qualify a timestamp-bearing price basis; ticker observation freshness cannot satisfy it.[^gate-ticker][^kraken-ticker]

Other undocumented behavior—boundary inclusion, sparse bars, ordering, revisions, closure latency and retention—must be handled explicitly, not all turned into invented mandatory vendor SLAs. Once the foundational shape/time/use gates are settled, bounded overfetch plus local filtering can avoid assuming query-bound inclusion; sorting can avoid depending on Gate order; exact expected-minute checks can fail closed on gaps; conflicting revisions can invalidate a snapshot. These are **proposed engineering mitigations**, not vendor guarantees or executed qualifications. They cannot resolve an unknown timestamp anchor or replace a missing closure contract.[^gate-spec][^kraken-ohlc]

### Feasible next implementation without secrets or financial inventions

**Recommended bounded follow-up: offline source-capability/admission review**, using the existing synthetic adapter/owner/native-projection seams described in [market data](../market-data.md). This is a proposal, not code or passing-test evidence:

* Add a dated, source-specific qualification record/reason projection: Gate schema unqualified; Gate REST time unqualified; Kraken REST time unqualified; ticker trade time unavailable; source-use approval absent. Keep these independent rather than one misleading `providerSupported` switch.
* Surface those reasons in a read-only source review/preview. Ordinary startup, opening settings, viewing a source or saving a draft must issue **no network request**. Keep production candles returning `UnsupportedConvention` and keep provider activation off.
* Keep any future **latest-observation-only** capability separate from complete-minute averages and event-age-based economic eligibility. Its technical contract is clearer, but its network activation still waits for applicable use/deployment approval; no credentials are needed or requested to test that separation.[^gate-ticker][^kraken-ticker]
* Use only invented responses and injected clocks for acceptance: seven-field `"true"` tail cannot become base volume; numeric seven-field tail cannot become closure; eight-field synthetic data cannot authorize a provider response; Kraken's original terminal row is excluded before sorting; unknown trade age stays unknown after a fresh ticker receipt; approving one provider/convention cannot approve the other; absent rights approval schedules no public jobs.
* Preserve separately labelled manual/offline declarations; do not relabel them as exchange observations, fill gaps with invented prices, assume USDT parity, or enable income/optimizer/mining actions. The existing [manual-input boundary](../manual-market.md) remains a different source class.

A future archive/import core can likewise be developed and tested with synthetic provenance, but an actual Kraken CSV archive is not ready-made qualification for REST or for a dense 1m FX series.[^kraken-download][^kraken-ohlc] This review does not unblock a real credit source; the independent [credit-provider limitations](market-and-credit-providers.md#7-kryptex-actual-credit-capability-assessment) remain outside this recheck.

**Bottom line:** no newly established evidence permits strict production candlestick admission. Useful autonomous progress remains possible on offline capability visibility/admission tests, without collecting private data or making up financial facts.

## Sources and reproducibility

All URLs below were reviewed or, where explicitly marked, attempted on **2026-11-06**. Dates are publisher dates when stated, otherwise the access date controls. Pinned SDK/spec commits and the raw-spec SHA-256 values identify the reviewed artifacts, not vendor guarantees of future behavior. Only schema/contract text is cited; documentation sample prices are not observations.

* **Gate OpenAPI snapshot:** v4.106.179; pinned `9df90626fca7d38e6d2e443efcb938807a4f7c44/openapi.yaml`; SHA-256 `a54c33060ea2147ed8c94fa8afe6b151d55217304ddc1ca540c5c867946c164e`.[^gate-spec]
* **Kraken raw Spot REST OpenAPI snapshot:** v1.1.0; SHA-256 `4e730d0c503de30a57dadd09fd07883af6aa3538e3bc4adabcd0e3576f1d0e1f`.[^kraken-spec]

[^gate-spec]: Gate official Go SDK, [pinned raw OpenAPI](https://raw.githubusercontent.com/gate/gateapi-go/9df90626fca7d38e6d2e443efcb938807a4f7c44/openapi.yaml) and [line-linked source](https://github.com/gate/gateapi-go/blob/9df90626fca7d38e6d2e443efcb938807a4f7c44/openapi.yaml#L29337-L29447), v4.106.179; `paths./spot/candlesticks.get`, response inner schema/description/example/security; also `info.license`, `info.termsOfService`, `servers` and `components.schemas.Ticker`. Accessed 2026-11-06.
[^gate-spot]: Gate, [international Spot REST API — Market K-line chart](https://www.gate.com/docs/developers/apiv4/en/spot/#market-k-line-chart), `GET /spot/candlesticks`, parameters, seven-element example, eight-item response description and no-auth statement. Accessed 2026-11-06.
[^gate-general]: Gate, [international API v4 general documentation](https://www.gate.com/docs/developers/apiv4/en/), current v4.106.179 changelog (2026-11-03), “Access URL,” “SDK,” “Comparison with APIv2,” “Time,” “API Gateway in/out time,” “Pagination,” and [Frequency limit rule](https://www.gate.com/docs/developers/apiv4/en/#frequency-limit-rule). Accessed 2026-11-06.
[^gate-python]: Gate official Python SDK, [pinned `spot_api.py`](https://github.com/gate/gateapi-python/blob/6ce527946f500060c41f9eeebade4835487baef8/gate_api/api/spot_api.py#L998) ([raw](https://raw.githubusercontent.com/gate/gateapi-python/6ce527946f500060c41f9eeebade4835487baef8/gate_api/api/spot_api.py)), `list_candlesticks` / `list_candlesticks_with_http_info`: nested string-list return type, request-bound parameter descriptions, empty authentication settings. Accessed 2026-11-06; no SDK code was executed.
[^gate-ws]: Gate, [Spot WebSocket v4 — Candlesticks Channel](https://www.gate.com/docs/developers/apiv4/ws/en/#candlesticks-channel), named `a`, Boolean `w`, missing-true warning and separate `t` description. Accessed 2026-11-06; documentation only, no subscription.
[^gate-ticker]: Gate, [Spot REST — Get currency pair ticker information](https://www.gate.com/docs/developers/apiv4/en/spot/#get-currency-pair-ticker-information), and `components.schemas.Ticker` in the [pinned official specification](https://raw.githubusercontent.com/gate/gateapi-go/9df90626fca7d38e6d2e443efcb938807a4f7c44/openapi.yaml): `last`, bid/ask, ETF rebalancing timestamp, no general trade-time field. Accessed 2026-11-06.
[^gate-trades]: Gate, [Spot REST — Query market transaction records](https://www.gate.com/docs/developers/apiv4/en/spot/#query-market-transaction-records), `GET /spot/trades`, public fields, `from`/`to`, `last_id`/`reverse` caveats and row timestamp fields. Accessed 2026-11-06; no market request.
[^kraken-spec]: Kraken, [raw Spot REST OpenAPI](https://docs.kraken.com/openapi/spot-rest.yaml), version 1.1.0; `paths./public/OHLC.get`, `components.schemas.tickData`, `components.schemas.ohlc`, `components.schemas.ticker`, `components.schemas.trade`. Accessed 2026-11-06; the reviewed raw file's OHLC operation starts at line 514 and `tickData` at line 11193.
[^kraken-ohlc]: Kraken, [Get OHLC Data](https://docs.kraken.com/api-reference/market-data/get-ohlc-data) and its [full Markdown/OpenAPI rendering](https://docs.kraken.com/api-reference/market-data/get-ohlc-data.md): original last entry current/uncommitted, 720-recent-entry hard bound, `interval`, `since`, cursor, eight-field `tickData` and operation `security: []`. Accessed 2026-11-06.
[^kraken-history]: Kraken, [Historical data](https://docs.kraken.com/exchange/guides/general/historical-data), OHLC parameters/row names, time-ordered Trades, forward cursor usage, gap discussion, and conflicting no-bulk-dump statement. Accessed 2026-11-06. General collection suggestions do not override the endpoint's hard OHLC depth bound or authorize IP-sharded evasion.
[^kraken-go]: Kraken official Go SDK, [pinned `pkg/spot/rest.go`, lines 532–546](https://github.com/krakenfx/api-go/blob/a8484bc5ec985fd5ce5bcc0580f659727d8f7603/pkg/spot/rest.go#L532-L546) ([raw](https://raw.githubusercontent.com/krakenfx/api-go/a8484bc5ec985fd5ce5bcc0580f659727d8f7603/pkg/spot/rest.go)), `OHLCRequest`, `OHLC`, generic map return type. Its publisher-owned repository is linked by the [official developer index](https://docs.kraken.com/llms.txt). Accessed 2026-11-06; not executed.
[^kraken-download]: Kraken Support, [Downloadable historical OHLCVT data](https://support.kraken.com/articles/360047124832-downloadable-historical-ohlcvt-open-high-low-close-volume-trades-data), updated 2026-11-02; field meanings, sparse intervals, REST link, archives and “About the data.” Accessed 2026-11-06; only the guide was read, no financial archive downloaded.
[^kraken-ws1]: Kraken, [WebSocket v1 Candles (OHLC)](https://docs.kraken.com/exchange/api-reference/spot-websocket-v1/ohlc.md), `epoc_last` and `epoc_end` definitions. Accessed 2026-11-06; not a REST contract.
[^kraken-ws2]: Kraken, [WebSocket v2 Candles (OHLC)](https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/ohlc.md), `interval_begin` definition. Accessed 2026-11-06; not a REST contract.
[^kraken-ticker]: Kraken, [Get Ticker Information — Markdown/OpenAPI](https://docs.kraken.com/api-reference/market-data/get-ticker-information.md), `components.schemas.ticker` / `AssetTickerInfo`, all field definitions and operation `security: []`. Accessed 2026-11-06.
[^kraken-trades]: Kraken, [Get Recent Trades — Markdown/OpenAPI](https://docs.kraken.com/api-reference/market-data/get-recent-trades.md), trade row description, count bounds, `last` polling cursor and operation `security: []`. Accessed 2026-11-06; no market request.
[^gate-terms]: Gate, [User Agreement](https://www.gate.com/legal/user-agreement), §§1.1, 1.3(h), 2.4–2.6, 10.1(d), 12.6, 16.2. Accessed 2026-11-06. No blanket geographical or market-data-use clearance is asserted.
[^gate-api-terms]: Gate, [agreement PDF](https://www.gate.com/docs/agreement.pdf), the `info.termsOfService` target in the pinned official OpenAPI. Retrieval/text-review attempted 2026-11-06; a direct HTTPS download recheck returned HTTP 403. No access-control workaround was attempted, and its contents remain unreviewed. Cited only as an additional linked terms-review gap, **not as evidence of its contents**.
[^kraken-terms]: Kraken, [Global Terms of Service](https://www.kraken.com/legal/global-terms), last updated 2026-10-30; binding terms §§8 “Our Content,” 9 “Restrictions,” 11 “Disclaimer of Warranties,” and regional applicability introduction. Accessed 2026-11-06; the convenience summary was not substituted for binding clauses.
[^kraken-limits]: Kraken Support, [What are the API rate limits?](https://support.kraken.com/articles/206548367-what-are-the-api-rate-limits-), updated 2026-10-01, “Public (REST Market Data).” Accessed 2026-11-06.
