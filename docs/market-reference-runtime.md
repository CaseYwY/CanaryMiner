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

Saved opt-in source/terms/capability controls, native helper deployment/trust/auxiliary egress, qualified live averages/latest
sources, archives, actual credit/reward collection, economic/forecast source wiring and production/physical qualification
remain unfinished. P-06 and the affected requirements remain partial.
