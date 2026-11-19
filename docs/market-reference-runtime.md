# Frozen native public-reference ownership (P-06d)

M-MARKET/M-UI/M-SUPERVISOR; R-MKT-001/002/003, R-SAFE-001/002, R-PRIV-001/002;
T-MARKET/T-UI/T-INTENT/T-PRIVACY; D-155.

**Default-empty supervisor seam and an independently constructed owner adapter, not public-feed activation.**
`MarketReferenceSink` exposes one explicit-parent start, a read-only native projection, memory-only revocation and separate
post-drain settlement. Its header carries no network client, URL, credential, provider JSON or filesystem configuration.
Ordinary CLI startup installs no factory, adds no switch/preset, requests no public data and remains unchanged.

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
Disabling a manual declaration does not start public polling in the current parent. This is a frozen admission boundary,
not the future complete source-selection/configuration UI. With no public factory, prior manual behavior remains.

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
requested, that session withholds both and reports the conflict. A saved public-only request shows polling
unavailable in ordinary builds while source-use and deployment approval are pending. Disabling the request from
inside a running session applies on the next start. `--no-market` suppresses both sources for its launch.

The request covers latest display references. One-hour coverage, reward data and economic-mode activation each
have separate qualifications. Economic mode defaults off.

The [Gate Spot REST contract](https://www.gate.com/docs/developers/apiv4/en/spot/#market-candlesticks) still lacks an
unambiguous returned time anchor and consistent candle row definition. [Kraken Spot REST](https://docs.kraken.com/api-reference/market-data/get-ohlc-data)
identifies the last row as provisional and leaves the other rows' timestamps unspecified as start or end.
[Kraken WebSocket v2](https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/ohlc) defines its own
`interval_begin` field. The WebSocket contract applies to that transport. No public-feed factory is admitted here.

The [2026-11-06 primary-source recheck](research/market-source-qualification-2026-11.md) does not settle Gate's REST candle
shape or either REST minute time anchor. Public-feed use rights, regional/deployment/trust approval and any required
trade-event-age basis remain separate. `ProviderResponse` candles still return `UnsupportedConvention`; only explicit
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
