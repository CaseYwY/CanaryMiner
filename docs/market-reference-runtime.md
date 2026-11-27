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

### Show trade event times in the Dashboard

Save **Public ticker request**, then use `--allow-public-trades` on an ordinary
Dashboard launch of an HTTPS-enabled build. Choose one latest-price source flag
per launch. The trade mode selects the newest timestamp in each returned Gate
and Kraken batch. It shows separate event and receipt times. PRL/USD requires
fresh observations on both sides and no more than 30 seconds between their
events or between their receipts. The view estimates ages from the end of the
current local UTC second and uses elapsed monotonic time to prevent a wall clock
rollback from making an expired trade current again. The worker records receipt
times in milliseconds.

The existing background owner applies its refresh, timeout and retry policy.
During a session, earlier trade times cannot move a source's cached event
backward or renew its receipt. Conflicting prices for a cached or newer event
withhold that source. A receipt clock fault stops the owner and withholds the
current values. Cached prices expire as either their event or receipt age
exceeds two minutes. `--no-market`, active manual source conflict,
missing launch permission and stop follow the same admission rules as tickers.

The launch can also include `--allow-public-candles` for the same separate
one-hour view. This trade reference is for display. Economic mode defaults off.

### Choose a resolver for one Dashboard launch

Add `--market-dns-server IP` alongside `--allow-public-tickers` or `--allow-public-trades` on an ordinary parent launch. Choose the IPv4 or unscoped IPv6 address of a DNS server on port 53, or `cloudflare-doh` for the [fixed encrypted resolver](#choose-a-resolver-for-one-public-sample). The selected resolver is fixed for that Dashboard session, including refreshes and retries. If the launch separately allows public candles, those requests use the same resolver.

The saved ticker request and all the usual Dashboard admission checks still apply. The option lives in the parent process and goes only to the public data helper. It is consumed before mining workers are launched. It is absent from Settings, provider reports and public stats. The resolver address remains visible to users who can inspect the parent's command line. The helper continues to require a public destination and normal HTTPS certificate and hostname verification.

### Show a public hour in the Dashboard

In an opt-in build, save the public ticker request and launch the ordinary Dashboard with `--allow-public-candles` and one latest-price source flag
(`--allow-public-tickers` or `--allow-public-trades`). The candle permission is per launch and covers
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
trade-event-age basis remain separate. The generic candle adapter used by the background owner accepts explicit fixture minute starts
and returns `UnsupportedConvention` for Gate and Kraken provider candles. The [standalone hourly review](#check-a-public-hour-without-mining)
can test Kraken's receipt-minute alignment as a candidate source on explicit request.
Ticker receipt age is never a fabricated trade-event age, one-hour coverage,
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

### Check public trade event times

In a build with public HTTPS enabled, run:

```text
canary-miner market-tickers --allow-public-trades
canary-miner market-tickers --allow-public-trades --save-review fills.cmt
canary-miner market-tickers --open-review fills.cmt
```

The explicit flag permits one request each for up to 20 Gate Pearl mainnet
`PEARL_USDT` trades and Kraken `USDTUSD` trades. These are public market-data
requests. The optional `--dns-server IP` applies to the sample. The command
selects the newest timestamp present in each returned batch. It shows the
provider's event time, the local receipt time, and their separate ages at the
cutoff. If equal newest event times give different prices, the source remains
unavailable. Gate's pair and any supplied trade quote must agree with the
requested market. Kraken must return the documented internal pair key
`USDTZUSD`. [Gate spot trades](https://www.gate.com/docs/developers/apiv4/en/spot/#query-market-transaction-records) · [Kraken recent trades](https://docs.kraken.com/api-reference/market-data/get-recent-trades)

Both receipts and events must be at most 120 seconds old at the cutoff. A
difference of more than 30 seconds between either the two event times or the
two receipts withholds the USD product. The product is an exact decimal
multiplication. A stale price can still appear with its age and an explicit
stale label. The command returns 0 for a complete pair, 3 for an unavailable
pair, and 130 for cancellation after confirmed cleanup.

Saving requires a complete pair. The local archive retains the original
responses and clocks. Reopening recalculates the capture offline at its
original cutoff. Earlier readers need an update to open the trade archive.
The returned batches establish only which entries the providers returned.
The reference carries no global completeness, candle coverage or execution
guarantee. It is independent of the opt-in Dashboard and economic-mode
settings.

### Archive and replay one public ticker sample

```sh
canary-miner market-tickers --allow-public-tickers --save-review tickers.cmt
canary-miner market-tickers --open-review tickers.cmt
```

Save requires both Gate and Kraken replies to pass their market identity and timestamp checks. Each observation
must be within 120 seconds of capture and the two observations must be at most 30 seconds apart. The checks use
UTC and the local monotonic clock. The archive stores the two original JSON responses, observation and receipt
times, and relative elapsed times. The report uses the sample's original cutoff and labels prices as historical.
An optional `--dns-server IP` applies only when sampling. Its value is omitted from the archive.

Use a new `.cmt` filename in a trusted local working directory. Save preserves any existing archive. Open works
offline on either build profile. It checks the checksum, validates the saved times, and recomputes the report
using the original cutoff. The command checks for cancellation before the final publication step.
Once published, the archive stays saved through terminal output errors. The checksum detects corruption.
It provides no signature or proof of a provider's
trade time. The command runs independently of Dashboard consent and economic-mode settings.

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

Add `--kraken-fallback` to authorize one optional request for Kraken `USDTZUSD` OHLC data.
The helper makes the request only after a valid, complete MEXC response and a valid Coinbase
response with fewer than 60 in-window minutes. It uses the same requested hour, DNS selection,
15-second per-request budget and 64 KiB body limit. Kraken must supply its own 60 aligned minutes.
The command checks that the timestamp of Kraken's uncommitted last row matches the current UTC
minute at receipt, then excludes that row. A mismatch, including one caused by a UTC minute rollover
during delivery, makes the Kraken response unusable for the sample. Kraken's published REST format
does not define the historical timestamp anchor. A USD mean using Kraken is labeled as a candidate that
infers minute starts from the receipt check. The report shows both FX outcomes. Use is subject to the terms and source limits in
the [provider review](research/market-source-qualification-2026-11.md).
[Kraken Spot REST OHLC](https://docs.kraken.com/api-reference/market-data/get-ohlc-data)

Exit code 0 indicates both means were available, 3 means a mean or the helper was unavailable, and 130 indicates a cancelled sample with confirmed helper cleanup. If cleanup is uncertain, the command exits with 3, withholds both means and admits no further request. The command leaves Settings, Dashboard polling and economic mode alone. Live provider response qualification and deployment acceptance remain open.

### Save and replay a public hour

Run the save command from a trusted private local directory. Use a new, simple ASCII basename ending in `.cmh`.

```text
canary-miner market-hour --allow-public-candles --save-review capture.cmh
canary-miner market-hour --allow-public-candles --kraken-fallback --save-review capture-with-fallback.cmh
canary-miner market-hour --open-review capture.cmh
```

A save requires all 60 aligned minutes from both providers and both exact means. A failed or incomplete sample
returns an error and publishes no review. If a sample finishes with incomplete coverage, stderr shows
the UTC window and both provider outcomes from that attempt. Stdout is reserved for a saved report.
A source marked `non-public destination refused` reached the address check before HTTPS.
Check the DNS response for that provider. Benchmark, private and other non-public
ranges remain refused. The version 2 archive holds both original response bodies byte for byte,
the selected window, command start and per-request times, the calculated report, and a SHA-256 checksum.
When the USD calculation uses Kraken, version 3 adds the original Kraken body and receipt times
alongside the MEXC and incomplete Coinbase receipts. Coinbase success still produces version 2.
The save preserves any existing archive or temporary sibling. If the command reports uncertain publication or cleanup,
check the archive and its temporary sibling. Reopen any published archive to verify it.

Opening the archived hour works in builds with public HTTPS disabled. It reads only the supplied archive,
checks the checksum, revalidates the source formats and request-time bounds, and recalculates both means.
For version 3 it also checks the original Coinbase gap and the full Kraken coverage.
The ages in the report are measured at its recorded capture time. The archive and checksum provide a way to
check the stored calculation and detect corruption. They carry no provider signature or current freshness claim.
Existing [user-declared reviews](manual-market.md#save-and-reopen-a-local-hour-review) remain readable.

### Choose a resolver for one public sample

A public sample can use one explicitly selected DNS server if the deployment's resolver returns a
non-public address for a provider. Enter one IPv4 or unscoped IPv6 address for ordinary DNS on
port 53. The `cloudflare-doh` preset uses Cloudflare's fixed DNS-over-HTTPS endpoint over
verified TLS. It sends the public provider's DNS name to Cloudflare. A resolver failure ends
the selected request. [Cloudflare DNS over HTTPS](https://developers.cloudflare.com/1.1.1.1/encryption/dns-over-https/)

```text
canary-miner market-tickers --allow-public-tickers --dns-server IP
canary-miner market-hour --allow-public-candles --dns-server cloudflare-doh --save-review capture.cmh
```

The flag applies only to the one-shot public invocation and requires the public consent flag. If
omitted, the helper uses its usual resolver configuration. The option does not enter Settings,
saved reviews or offline replay. The Dashboard has its own [per-launch flag](#choose-a-resolver-for-one-dashboard-launch). A DNS answer must still resolve to an allowed
public destination, and HTTPS still verifies the provider name and certificate. The selected
resolver selector is visible to users with access to the command line. The encrypted preset
requires a public HTTPS worker with libcurl 8.16 or later. Candidate packages use the pinned
worker. [libcurl TLS version defaults](https://curl.se/libcurl/c/CURLOPT_SSLVERSION.html)

### Review the next Pearl mainnet subsidy

The live command queries the Pearl Research Labs Blockbook indexer in order. It reads `/api/status`,
asks `/api/v2/block-index/{height}` for the indexed hash at the reported height, then reads `/api/status`
again. The three requests share one 15-second sampling budget and one helper-ownership lifetime.
Use a build with public HTTPS enabled. The optional DNS server is scoped to this sample. The saved-file command opens
a user-selected raw status response with a declared UTC cutoff in Unix seconds. It starts no HTTPS helper.

```text
canary-miner market-reward --allow-public-reward [--dns-server IP]
canary-miner market-reward --file blockbook-status.json --as-of UTC_SECONDS
```

The command checks Pearl mainnet identity, eight-digit grain precision, synchronized index and backend
heights, and a valid index update time no more than 30 minutes before the observation or declared
cutoff. The provider exposes index update time as `lastBlockTime`, which records synchronization
activity. Live mode also requires both status responses to agree on height and backend tip hash, and
the indexed hash at that height to match. An advance, an observed same-height reorganization, or a
hash mismatch rejects the sample without retrying. The result records agreement among these three
responses from one provider. A later reorganization can still change the tip. The live checks use
each status response's whole observation second. The raw saved-file mode uses only the selected
status response and treats the supplied cutoff as an exact instant, rejecting even a fractional
second after it. [Pearl Blockbook contract](https://github.com/pearl-research-labs/blockbook/blob/master/openapi.yaml) · [Status time source](https://github.com/pearl-research-labs/blockbook/blob/master/api/worker.go#L2691-L2748)

For next height `h`, calculate the integer subsidy in grains by flooring
`S × H / ((h + H) × (h + H − 1))`. The supply cap `S` is 210,000,000,000,000,000 grains and
`H = floor(4 × 365 × 86400 / 194) = 650226`. Divide by 100,000,000 to display eight decimal places in
PRL. The report covers scheduled consensus subsidy. Transaction fees, pool payouts, each miner's credited earnings
and profit forecasts require their own data. [Pearl v1.5.8 subsidy implementation](https://github.com/pearl-research-labs/pearl/blob/3db05e0de0ca90c3a043abd927b968cf53bcd4c8/node/blockchain/validate.go) · [Pearl mainnet target interval](https://github.com/pearl-research-labs/pearl/blob/3db05e0de0ca90c3a043abd927b968cf53bcd4c8/node/chaincfg/params.go)

Exit code 0 means a valid subsidy reference. Exit code 1 means a report write failed. Exit code 2 means invalid input flags, 3 means an unusable
source or review, and 130 means a cancelled sample or review. A public sample with unconfirmed helper
cleanup returns 3. A rejected response never supplies a subsidy or changes financial settings.

### Archive and replay the subsidy query

Run these commands from a trusted private local directory. The save name must be a new ASCII basename ending in `.cmr`.

```text
canary-miner market-reward --allow-public-reward --save-review capture.cmr
canary-miner market-reward --open-review capture.cmr
```

A save records all three original response bodies in request order, each request's start, observation and completion seconds, and the exact report in a version 2 archive. The SHA-256 checksum covers the entire record and detects local corruption. The response's origin depends on the original HTTPS request. Treat the archive as a local record. The optional resolver flag applies to the live request only. Capture publishes a new file after the responses pass the same checks as the live report. It preserves an existing archive or temporary sibling. If stdout fails after publication, the command returns 1 and the archive remains available for replay. After an uncertain publication, inspect both files before retrying.

Replay works without public HTTPS support. It validates the archive, checks the ordering and range
of the recorded times, and repeats the height and hash checks at the original observation times.
The recomputed report must match the saved report. Version 1 archives remain readable with their
original status-only report. Capture and replay have no access to account payouts or income settings.

### Native ownership during one-shot ticker and hour sampling

Each ticker or hour invocation gives both of its requests one native ownership lifetime. The request threads carry that original lifetime when they start. A helper fault in the same application invalidates the sample even if the fault is settled before the next request. The command withholds its report, including any earlier successful observation, and returns 3. An in-flight helper receives cancellation and retains its existing bounded cleanup behavior.

The CLI regression uses the real native process client with an installed synthetic worker. It checks stale caller scopes on Windows and Linux. On Linux it also changes the ownership epoch at the first or second request-thread creation. A valid sample launches both workers. A stale caller or first-request fault launches none, and a second-request fault launches only the first. These cases use no provider connection.
