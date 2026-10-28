# Exact market adapters, cache owner and native projection (P-06b)

**Implemented offline-capable core; runtime public feeds remain unconfigured/default off.** This batch extends R-MKT-001–003, T-MARKET/T-INTENT and the native read-only projection. It is not a production market-data, income, optimizer or release qualification. P-06 is still `PARTIAL`. The later [P-06c manual/offline subpage](manual-market.md) supplies independent user declarations, not qualified provider observations.

## Exact data and identities

- `exact_decimal.h/.cpp` implements bounded, canonical, signed base-10 values: at most 96 coefficient digits and scale 48. Strict fixed-point input only; no exponent, whitespace, implicit locale, NaN, guessed atomic precision or silent overflow/rounding. Checked add/subtract/multiply leave output unchanged on failure. Market input has a narrower 38-digit/18-scale budget; this is a parser capability, **not Pearl's atomic-unit scale**.
- Means retain **exact decimal sum / integer count**. Nonterminating means are not silently stored as rounded decimals. Only presentation explicitly requests round-half-even, up to 24 places; a positive mean rounded below six displayed places is shown as `<0.000001`, not a fabricated zero.
- Reviewed namespaces are fixed: Gate international `PEARL_USDT` is the explicitly mapped Pearl-mainnet-native PRL alias, in USDT per PRL; Kraken `USDTZUSD` is USD per USDT, not its inverse, USDC or a peg. This uses [documentation/metadata identity evidence](research/market-and-credit-providers.md), not chain-genesis or custody verification.
- Gate ticker requires one matching pair; Kraken requires its exact market key and an empty error envelope. Last price and optional bid/ask are different values; positive prices, nonnegative volumes, correct JSON types and noncrossed sides are checked. Known Kraken `EAPI:Rate limit exceeded` is a structured rate-limit result even at HTTP 200; other error messages are never copied into terminal/log output.
- Each observation retains source namespace/origin, request-start/receive UTC, monotonic receipt, and generation. A response cannot precede the request or claim a future receipt. Neither ticker supplies trade-event time: it stays absent, including when ETF/gateway/volume/cursor fields are present. An event-time-required policy receives unavailable, not an invented fresh trade.

## Candle and complete-window boundary

**No production candle-time assumption was qualified in this batch.** Both REST time anchors and Gate's seven/eight-position inconsistency remain gates. `ProviderResponse` candles are rejected as `UnsupportedConvention`; the only accepted minute-start convention is explicitly `SyntheticFixture` + `FixtureMinuteStart`. This cannot enable live averages by toggling a fixture flag alone.

For the offline dialect:

- Gate rows have exactly eight positions and a literal string `"true"`/`"false"` closure marker; no guessed seven-position layout, JSON-boolean coercion or missing base volume.
- Kraken uses index 4 for close, not Gate's index 2. Its **original last row** is excluded before sorting even if its timestamp looks old. A validated cursor is retained only with a successful response.
- Reject duplicates, non-minute/millisecond/future timestamps, invalid OHLC bounds, negative volume and oversized row counts (Gate 1000 / Kraken 720). Valid zero-volume bars are allowed. Closed bins must have ended before receipt. Sorting does not repair duplicate/conflicting facts.
- Window intervals are `[S,E)` with each expected complete one-minute start required. A missing/incomplete minute or unrelated old row cannot be replaced by zero/forward-fill. The public calculation seam revalidates sorted/unique/domain constraints rather than trusting mutable structs blindly.
- Historical USD is `sum(P_i * F_i) / count` over matching minute keys. Missing/invalid FX leaves complete USDT available but USD unavailable. The alternating synthetic `(1,1),(3,3)` example yields 5, **not** product-of-means 4. Latest conversion separately yields `2.50 * 0.98 = 2.45`; these are invented test values only.
- Display uses `E=floor((nowUtc-guard)/60)*60`, `S=E-3600`: the last 60 complete UTC bins through E, not a second-exact sliding hour ending now. Economics has an independent initial 480-minute lookback, configurable 1–719; the shared candle request covers `max(60,economicsMinutes)`. Longer minute history is unsupported without a separately qualified archive/provider. No fake pagination, interval enlargement or historical coverage is inferred.

## Scheduling, ownership and stale results

`MarketPollingPlan` is deterministic and has no I/O. `MarketDataOwner` is one explicit-start background owner, with one in-flight request and four bounded cache slots. It publishes immutable snapshots; the renderer does no network/JSON/accounting work. Construction or settings rendering never starts a request/miner. The application must use one owner for installation-wide feeds, **not a separate owner per GPU**.

Production fetch wiring is supplied by `marketProcessFetch(installedHelper)`, which calls the [P-06a process client](public-https.md), not the potentially blocking low-level TLS client inside the miner. The owner is not yet constructed by the production supervisor. Injected test callbacks must obey the same bounded cancellation contract; joining an arbitrary callback that ignores cancellation is not a promised hard deadline. Start/stop are single-controller lifecycle calls; snapshots are thread-safe.

Independent defaults/bounds:

| Clock/control | Initial policy | Boundary |
|---|---|---|
| Latest quote refresh | 60 seconds | independently 1 second–24 hours |
| Candle refresh | 60 seconds | independent from quote refresh; no live candle requests while unqualified |
| Observation expiry | 120 seconds | checked against UTC **and** monotonic age; equality is eligible |
| Latest cross-feed skew | 30 seconds | both observation clocks and generation/origin must match |
| Settlement guard | 5 seconds | 0–60 seconds; cannot replace a closure marker |
| Economics lookback | 480 complete minutes | independent 1–719; display remains 60 |
| Economics request clock | explicit data start, then hourly | independently 1 second–24 hours; coalesces missed ticks |
| Public pacing | at least 1 second per provider | shared across that provider's ticker/candle requests, one global in-flight request |
| Retry cycle | up to 3 attempts | configurable 1–5, exponential 5-second base, 5-minute retry cap/cycle cooldown |

These are local engineering defaults, not provider SLAs, measured profitability or user-specific values. Economics ticks only publish **evaluation requests**: no candidate optimizer or mining action is implemented.

Only network/timeouts, HTTP 429/5xx and the recognized API rate-limit envelope retry. Respect numeric Retry-After, with finite attempt cycles and cooldown. A transport-capped 3600-second 429 refusal is conservatively latched rather than retried before an unknown longer refusal. Certificate/protocol/address/config/schema/identity failures latch that feed until a fresh explicit data generation; they never switch origin, request credentials or downgrade HTTPS. Overdue selection prevents a fast ticker from starving candle work.

The cache retains a last good observation with its original times through failures; no error creates zero/fresh facts. Publication checks full ticket/generation identity, and cancellation is serialized with fact commit. Stop cancels pending work, discards late responses and does not rearm polling; restart requires a newer explicit generation and clears old source caches. Monotonic rollback, UTC rollback or UTC/monotonic drift over five seconds stops the owner with a clock fault. No market event changes a miner session token, GPU, pool/donation lane or actual-credit fact.

## Native terminal seam and preview

`PublicMarketView` is independent of `FinancialView`: latest PRL/USDT, USDT/USD, USD reference, source health, observation time/age, absent trade-event time, 1h means and actual coverage/end/lag are projected from validated snapshots. A default/unstarted owner does not enable a section. Stopped/stale/missing-FX/clock-fault values have textual unavailable reasons, not color-only status. No credited-income or net-profit rows are enabled as a side effect.

The existing dashboard has a `PUBLIC MARKET REFERENCES` section only when supplied that projection. **The supervisor does not supply provider projections yet.** P-06c instead uses a separately headed manual-declaration section. CPU-only `canary_market_preview` supplies conspicuously labelled synthetic fixtures to exercise the provider seam without a GPU, mining, credentials or network:

```sh
cmake --preset tests -B build/market-check
cmake --build build/market-check --target test_exact_decimal exact_decimal_probe test_market_data test_market_owner canary_market_preview
ctest --test-dir build/market-check -R '^(exact_decimal|exact_decimal_oracle|market_data|market_owner|market_dashboard)$' --output-on-failure
build/market-check/tests/canary_market_preview fresh 80
```

Use the executable suffix on Windows. Preview modes are `fresh`, `stale`, `missing-fx`, `clock-fault`; every value is invented. Tests cover 60/80/128 columns. The existing renderer clamps a 40-column request to 60: this batch does **not** claim full sub-60-column support. No independent GUI/browser runtime is added.

The original P-06b slice introduced no settings/CLI flags, saved config/schema, cache file, history migration, provider authentication or automatic startup behaviour. P-06c's separate preferences/CLI and rollback are documented in [manual/offline inputs](manual-market.md); neither slice enables public polling. Do not delete accepted history, mining credentials or runtime preferences.

## Evidence and remaining work

Executed evidence, using synthetic/offline inputs only:

- Windows/MSVC and Linux/WSL/GCC: **20,302 exact-decimal checks, 87 provider/window checks and 111 scheduler/background-owner checks** per platform; zero failures. Arithmetic additionally passes **1500 seeded Python-Decimal oracle cases / 7500 exact comparisons** per platform.
- Native preview: **12 mode/width scenarios per platform**, plus the separately asserted existing 40-to-60-column clamp. Covers fresh, stale, missing FX and clock fault without enabling income/net rows.
- Windows backend-disabled focused CTest regression: **28/28 passed**, including existing product/settings/history/intent/launcher cases and architecture contracts. Backend-enabled combined HTTPS/market selection: **7/7 passed**. A first combined invocation lacked the Visual Studio include environment; rerunning inside the established toolchain script passed. This is not full production/Python discovery qualification.
- Linux ASan+UBSan: all three new C++ suites above pass with no sanitizer finding. Rebuilt existing display/settings/synthetic-supervisor suites pass **77/143/213 checks** respectively; architecture contracts **4/4**. Linux builds here use direct GCC commands, not Linux CMake qualification.

Checks include boundary/rounding/property cases, source/currency/candle/coverage checks, deterministic clock/backoff/rate/stop behaviour and actual background-owner immutable-cache/cancellation scenarios, including all four fixture feeds. No hardware miner was started by these tests.

Not run or supplied by this original slice: live ticker/candle/metadata qualification, source terms/regional access approval, native trust auxiliary egress/packaging/signing, persistent candle archive, provider supervisor/settings activation, actual-credit source/runtime integration, native GPU/game or release qualification. Later [manual input](manual-market.md), [credit core](credit-ledger.md), [credit owner](credit-owner.md) and [civil/energy/tariff core](electricity-core.md) have their own bounded evidence, not live financial qualification. Full project scope remains in [execution plan](architecture/execution-plan.md). The existing financial presentation contract remains disabled; no shares, balances or payouts are relabelled as income.
