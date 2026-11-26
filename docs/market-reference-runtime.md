# Frozen native public-reference ownership (P-06d)

M-MARKET/M-UI/M-SUPERVISOR; R-MKT-001/002/003, R-SAFE-001/002, R-PRIV-001/002;
T-MARKET/T-UI/T-INTENT/T-PRIVACY; D-155.

P-06d introduced a default-empty supervisor seam and a separate owner adapter.
`MarketReferenceSink` exposes one explicit-parent start, a read-only native projection, memory-only revocation and separate
post-drain settlement. Its header carries no network client, URL, credential, provider JSON or filesystem configuration.
Default builds leave the public factory empty.

`makeMarketReferenceRuntime` freezes its owner policy/origin/convention/clock and injected fetch contract. Construction
and rendering do no fetch work; only the one-use explicit start admits the existing bounded background market owner.
Invalid/zero/failed admission consumes that runtime's attempt; stop before start is terminal. The adapter cannot restart
under a later generation or convert a synthetic candle convention into a provider convention. This one-use restriction
belongs to the parent adapter; the existing lower-level owner's explicit newer-generation restart API remains compatible.

`MarketDataOwner::requestStop` serializes cancellation against cache commit and notifies the background worker without
joining it. Its retained snapshot is an original last observation, not proof that the callback has exited. The adapter
immediately projects stopped/unavailable current references even while a callback is still outstanding. Old cache values
keep their original source/time and stale/invalid labels; a late successful reply cannot commit facts after revocation.
`stop` joins separately. Injected callbacks must obey their bounded cancellation contract; this does not turn an arbitrary
callback or native kernel/trust stall into a hard exit-time guarantee.

[P-06e](public-helper-ownership.md) adds whole-owner terminal admission on a parent-only Unreaped fetch outcome, including
one delivered during cancellation. Neither a second feed, an economic refresh nor a newer generation may revive that owner;
retained original quotes remain stopped references, not current authority. The helper's retained quarantine is module-local.

The supervisor constructs at most one optional sink for a dashboard parent, not one per GPU or child retry. `--no-market`,
plain mode and an already requested stop prevent construction. A signal raised during factory/start admission prevents
source activation/worker revival. Failed or thrown admission uses fixed non-private notices and never silently falls back
to another source. The sink is revoked with intent before worker drain and joined afterward; no source data enters public
stats or the worker command/protocol/donation paths. Optional market joining follows the actual controlled-fleet `stop`
settlement callback too, not merely a reported worker exit or memory-only revocation.

Public requests and saved enabled manual references cannot silently mix. If both are requested at parent startup, no public
factory is called and both reference projections are withheld with an explicit source-conflict notice. Existing manual
files/times remain untouched. While a public source is requested for this parent, enabling manual references through the
live manual-save path refuses before store writes or live replacement; changing the selected source requires a new explicit parent.
Disabling a manual declaration does not start public polling in the current parent. With neither a saved public request
nor a supplied public factory, the manual display follows its saved settings.

## Source qualification is still closed

**Settings → Market and timing → Public feed readiness** shows the two expected public series, their latest-price and
one-hour evidence separately, and the use/deployment status. The review is read-only and works with `--no-market`.
The manual reference editor keeps its own entry and save target.

### Save a public ticker request

**Settings → Market and timing → Public ticker request** configures the request for Gate PRL/USDT and Kraken
USDT/USD latest prices. Space changes the draft, V shows its old and proposed values, and Enter confirms the
save. The default is off, including for existing configurations. Saving off removes the optional
`public_tickers_requested` field from `runtime-preferences.json`. Use that off setting before downgrading to
a strict decoder that predates the field.

The next explicit Dashboard session reads the saved request. When both public tickers and manual references are
requested, that session withholds both and reports the conflict. Disabling the request from inside a running session
applies on the next start. `--no-market` suppresses both sources for its launch.

The request covers latest display references. One-hour coverage, reward data and economic-mode activation each
have separate qualifications. Economic mode defaults off.

### Run the opt-in latest ticker path

A build with `CANARY_ENABLE_PUBLIC_HTTPS=ON` can serve a saved request when an ordinary Dashboard launch includes
`--allow-public-tickers`. The permission lasts for that launch. Startup without the flag leaves the saved request
pending. Plain mode, automatic tuning, worker processes and `--no-market` withhold polling. The parent consumes the permission flag
before assembling worker arguments. The installed helper must be beside the executable in its protected directory.

The path uses only the Gate `api.gateio.ws` ticker for `PEARL_USDT` and the Kraken `api.kraken.com` ticker for
`USDTUSD`. The Dashboard labels local observation times and expires stale references. It uses the existing bounds
for refresh, timeout and retry. The ticker path excludes minute candles and economic scheduling. The default
CMake build and current release policy keep the HTTPS capability off.

### Show a public hour in the Dashboard

In an opt-in build, save the public ticker request and launch the ordinary Dashboard with both
`--allow-public-tickers` and `--allow-public-candles`. The second permission is per launch and covers
MEXC `PEARLUSDT` and Coinbase `USDT-USD` minute candles. A ticker-only launch keeps its existing request set.
Plain output, automatic tuning, `--no-market`, and a conflicting manual request suppress the public Dashboard path.

The background owner refreshes candles about once a minute. When both sources are ready, it requests the same
fixed UTC window from each. Each source shows its own elapsed-minute coverage, receipt age and latest request status.
The one-hour mean is a TWAP approximation using equally weighted minute closes. USDT per PRL requires all 60 Pearl minutes. USD per PRL also
requires 60 FX closes for the exact same minutes. The display labels the exact UTC bounds and marks a previous
minute window while waiting for the next update. The default age limit is two minutes on both local clocks.
The hour remains a candidate reference with unverified candle finality. Economic mode defaults off and consumes
no data from this display.

The installed-path test uses a synthetic helper beside an inert executable. Live provider requests, source-use
approval and helper trust/deployment qualification require separate evidence.

### Check public tickers without mining

Run `canary-miner market-tickers --allow-public-tickers` from a build with public HTTPS enabled. The explicit flag permits one request each to the existing Gate Pearl mainnet ticker and Kraken USDT/USD ticker. The command exits after the sample. It leaves Dashboard and economic-mode settings untouched.

The output shows the exact prices, source status and local observation times. USD per PRL requires fresh and closely timed observations from both sources. Providers do not supply a trade timestamp through these adapters. The command returns 0 for a complete fresh pair, 3 for an unavailable or incomplete sample, and 130 for cancellation with confirmed helper cleanup. If helper cleanup cannot be confirmed, the command stops sampling and withholds both quotes. It does not request candles or retry either provider. Use a protected installation with the approved helper beside the executable. The same public-source use and deployment requirements apply.


The [Gate Spot REST contract](https://www.gate.com/docs/developers/apiv4/en/spot/#market-candlesticks) still lacks an
unambiguous returned time anchor and consistent candle row definition. [Kraken Spot REST](https://docs.kraken.com/api-reference/market-data/get-ohlc-data)
identifies the last row as provisional and leaves the other rows' timestamps unspecified as start or end.
[Kraken WebSocket v2](https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/ohlc) defines its own
`interval_begin` field. The WebSocket contract applies to that transport. Neither candle adapter is admitted for public polling.

The [2026-11-06 primary-source recheck](research/market-source-qualification-2026-11.md) does not settle Gate's REST candle
shape or either REST minute time anchor. Public-feed use rights, regional/deployment/trust approval and any required
trade-event-age basis remain separate. Gate and Kraken `ProviderResponse` candles still return `UnsupportedConvention`; only explicit
synthetic fixture minute starts are accepted. Ticker receipt age is never a fabricated trade-event age, one-hour coverage,
credited income, reward calibration, net estimate or economic/mining permission.

The injected tests exercise synthetic quote responses, missing/invalid/one-use admission, frozen policies,
source/time/stale/clock behavior and a deliberately held callback across non-joining revocation. Native fake-worker tests
exercise suppression, per-parent ownership, signal/failure/exception paths and manual-source conflict with preserved files.
They load no NVML driver and do not open an exchange, wallet/account, pool, game or GPU. Exact platform/repetition/sanitizer/
CLI/PTY/privacy results are recorded in the [execution ledger](architecture/execution-plan.md), not inferred from this design.
The actual-owner/parser PTY separately checks a live manual-enable refusal without file writes, source-conflict and CLI
suppression with exact original files, unchanged inert-child start count and a real controlling-terminal SIGINT.

Source-use and capability approval, native helper deployment/trust/auxiliary egress, qualified live averages/latest
sources, archives, actual credit/reward collection, economic/forecast source wiring and production/physical qualification
remain unfinished. P-06 and the affected requirements remain partial.

### Review saved ticker payloads

Use `canary-miner market-tickers --file FILE` to review a local document on either
build profile. Use a regular JSON file of at most 64 KiB in a trusted local directory.
Each supplied source
contains its declared observation UTC second and the original JSON response encoded
as a string. Supply either source or both. The cutoff must be at or before the local
clock, and each observation must be at or before the cutoff.

```json
{
  "schema_version": 1,
  "as_of_utc": 1760000060,
  "gate": {
    "observed_utc": 1760000058,
    "response_json": "[{\"currency_pair\":\"PEARL_USDT\",\"last\":\"2.5\"}]"
  },
  "kraken": {
    "observed_utc": 1760000060,
    "response_json": "{\"error\":[],\"result\":{\"USDTZUSD\":{\"c\":[\"0.98\",\"1\"]}}}"
  }
}
```

The command uses the provider price parsers. A source qualifies for this historical
calculation if its declared observation is within 120 seconds of the cutoff. The
USD value is the exact product of the two eligible last prices when the observations
are at most 30 seconds apart. The same file produces the same report at that cutoff.
Exit code 0 means the pair produced a USD reference, 3 means it was unavailable
or the file failed validation, and 130 means the review was cancelled. The output
identifies the figures as declared and historical. The saved file supplies no
authenticated source, trade-event time, credentials or permission to activate
Dashboard or economic mode.

### Check a public hour without mining

Run `canary-miner market-hour --allow-public-candles` from a public-HTTPS-enabled installation. The permission applies to this invocation. Use the approved helper installed beside the miner in a protected directory. The helper requests one minute series per fixed market, MEXC `PEARLUSDT` and Coinbase `USDT-USD`, for the same 60-minute window. The exclusive end is a UTC minute boundary at least five seconds before the initial local time. Each request has a 15-second budget and a 64 KiB response-body limit. The command makes no retries. [MEXC candle format](https://www.mexc.com/api-docs/spot-v3/market-data-endpoints/klinecandlestick-data) · [Coinbase candle format](https://docs.cdp.coinbase.com/api-reference/advanced-trade-api/rest-api/public/get-public-product-candles)

The report names each source, the UTC window, minute coverage, local receipt times and ages, and exact sums divided by 60. The USDT mean requires every Pearl minute. The USD mean additionally requires a valid FX close for each of those exact minutes. The report labels candle finality as unverified. A missing or malformed response gets a bounded reason, while an independent complete Pearl series can still supply the USDT mean.

Exit code 0 indicates both means were available, 3 means a mean or the helper was unavailable, and 130 indicates a cancelled sample with confirmed helper cleanup. If cleanup is uncertain, the command exits with 3, withholds both means and admits no further request. The command leaves Settings, Dashboard polling and economic mode alone. Live provider response qualification and deployment acceptance remain open.

### Save and replay a public hour

Run the save command from a trusted private local directory. Use a new, simple ASCII basename ending in `.cmh`.

```text
canary-miner market-hour --allow-public-candles --save-review capture.cmh
canary-miner market-hour --open-review capture.cmh
```

A save requires all 60 aligned minutes from both providers and both exact means. A failed or incomplete sample
returns an error and publishes no review. If a sample finishes with incomplete coverage, stderr shows
the UTC window and both provider outcomes from that attempt. Stdout is reserved for a saved report.
A source marked `non-public destination refused` reached the address check before HTTPS.
Check the DNS response for that provider. Benchmark, private and other non-public
ranges remain refused. The version 2 archive holds both original response bodies byte for byte,
the selected window, command start and per-request times, the calculated report, and a SHA-256 checksum.
The save preserves any existing archive or temporary sibling. If the command reports uncertain publication or cleanup,
check the archive and its temporary sibling. Reopen any published archive to verify it.

Opening the archived hour works in builds with public HTTPS disabled. It reads only the supplied archive,
checks the checksum, revalidates both source formats and request-time bounds, and recalculates both means.
The ages in the report are measured at its recorded capture time. The archive and checksum provide a way to
check the stored calculation and detect corruption. They carry no provider signature or current freshness claim.
Existing [user-declared reviews](manual-market.md#save-and-reopen-a-local-hour-review) remain readable.

### Choose a resolver for one public sample

A public sample can use one explicitly selected DNS server if the deployment's resolver returns a
non-public address for a provider. Substitute the IPv4 or unscoped IPv6 address of your approved
resolver for `IP`. It must answer ordinary DNS on port 53.

```text
canary-miner market-tickers --allow-public-tickers --dns-server IP
canary-miner market-hour --allow-public-candles --dns-server IP --save-review capture.cmh
```

The flag applies only to the one-shot public invocation and requires the public consent flag. If
omitted, the helper uses its usual resolver configuration. The option does not enter Settings,
saved reviews, offline replay or Dashboard polling. A DNS answer must still resolve to an allowed
public destination, and HTTPS still verifies the provider name and certificate. The selected
resolver address is visible to users with access to the command line.

### Review the next Pearl mainnet subsidy

The live command makes one request to the documented `/api/status` endpoint of the Pearl Research Labs Blockbook indexer. Use a build with
public HTTPS enabled. The optional DNS server is scoped to this sample. The saved-file command opens
a user-selected raw status response with a declared UTC cutoff in Unix seconds. It starts no HTTPS helper.

```text
canary-miner market-reward --allow-public-reward [--dns-server IP]
canary-miner market-reward --file blockbook-status.json --as-of UTC_SECONDS
```

The command checks Pearl mainnet identity, eight-digit grain precision, synchronized index and backend
heights, and a valid index update time no more than 30 minutes before the observation or declared
cutoff. The provider exposes index update time as `lastBlockTime`. It records synchronization activity
and has no block-header time or finality guarantee. The report labels the indexed height and the
backend hash separately. Equal heights can occur during a same-height reorganization. Live mode labels
the HTTPS helper's local observation time. Offline mode labels the supplied cutoff. [Pearl Blockbook contract](https://github.com/pearl-research-labs/blockbook/blob/master/openapi.yaml) · [Status time source](https://github.com/pearl-research-labs/blockbook/blob/master/api/worker.go#L2691-L2748)

For next height `h`, calculate the integer subsidy in grains by flooring
`S × H / ((h + H) × (h + H − 1))`. The supply cap `S` is 210,000,000,000,000,000 grains and
`H = floor(4 × 365 × 86400 / 194) = 650226`. Divide by 100,000,000 to display eight decimal places in
PRL. The report covers scheduled consensus subsidy. Transaction fees, pool payouts, each miner's credited earnings
and profit forecasts require their own data. [Pearl v1.5.8 subsidy implementation](https://github.com/pearl-research-labs/pearl/blob/3db05e0de0ca90c3a043abd927b968cf53bcd4c8/node/blockchain/validate.go) · [Pearl mainnet target interval](https://github.com/pearl-research-labs/pearl/blob/3db05e0de0ca90c3a043abd927b968cf53bcd4c8/node/chaincfg/params.go)

Exit code 0 means a valid subsidy reference. Exit code 1 means a report write failed. Exit code 2 means invalid input flags, 3 means an unusable
source or review, and 130 means a cancelled sample or review. A public sample with unconfirmed helper
cleanup returns 3. A rejected response never supplies a subsidy or changes financial settings.

### Native ownership across a one-shot sample

Both requests share one native ownership lifetime when an explicit command starts. The request threads carry that original lifetime when they start. A helper fault in the same application invalidates the sample even if the fault is settled before the next request. The command withholds its report, including any earlier successful observation, and returns 3. An in-flight helper receives cancellation and retains its existing bounded cleanup behavior.

The CLI regression uses the real native process client with an installed synthetic worker. It checks stale caller scopes on Windows and Linux. On Linux it also changes the ownership epoch at the first or second request-thread creation. A valid sample launches both workers. A stale caller or first-request fault launches none, and a second-request fault launches only the first. These cases use no provider connection.
