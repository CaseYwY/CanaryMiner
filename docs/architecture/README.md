# CanaryMiner desktop automation: architecture book

**Architecture version:** 1.1. **State:** accepted product requirements plus a proposed, staged architecture. An architecture requirement is not an implemented feature.

**Implementation baseline:** `ab208e8` adds native terminal settings and four presentation preferences. It is a fork development commit, not an upstream release. The dashboard-metrics branch starts from that baseline. Read the implementation ledger before executing work.

**Latest bounded slice: P-10g.** [Receipt-bound temporal references](../economic-choice-guard.md) join the actual evaluator/issuer under one identical parent and frozen worker. Original evaluation times drive confirmation; original application receipts drive cooldown/budgets, with explicit cached-result and unsafe-flight revocation. Outputs remain reference-only. Full decision-to-command binding, budgets across replacement workers, complete safety/financial qualification, ordinary activation and physical GPU/game acceptance remain open. The [execution ledger](execution-plan.md) is authoritative; older slice summaries below are historical context.

**Display batch:** `a2549c3` adds Details/Log controls and the synthetic-input financial presentation contract. See [display validation](../display-validation.md); live financial providers are still absent.

**History batch:** P-04 adds local observed accepted counters with optional system SQLite, rolling windows, coverage/recovery and `--no-history`. See [history semantics](../accepted-history.md) and [validation](../history-validation.md). It is not a complete pool-acknowledgement or token-income ledger.

**Safety slices:** P-03a adds [session intent and launcher lifecycle](../session-intent.md). Intentional stop and latched worker faults no longer authorize unconditional relaunch by the checked-in advanced scripts. P-03b adds a [manual complete-work-batch host wait](../work-wait.md), default 0 ms. Live runtime activation, measured profiles and automatic policy integrations remain unfinished; native GPU qualification is still required.

**Runtime settings:** P-03c adds [native persisted host-wait/history defaults](../runtime-settings.md) with review/cancel, configured/effective/CLI distinctions and separate save targets. Both activate only at the next explicit session. Live activation and wider mining-config review remain unfinished.

**Public data transport:** P-06a adds a [default-off HTTPS/helper-process foundation](../public-https.md), with bounded local TLS/IPC tests on both host platforms. No market polling, financial provider or Stratum TLS is enabled; cache/adapters, native-trust egress and release packaging remain unfinished.

**Market core:** P-06b adds [exact price/FX adapters, immutable cache ownership and a native projection](../market-data.md), verified with synthetic/offline data. No production supervisor activates the public-feed owner; provider candles, income and native-trust/release gates remain unfinished.

**Offline references:** P-06c adds [native manual price/FX input](../manual-market.md), independent review/save, freshness/skew labels and a one-session `--no-market` override. Running terminal display can update without restarting workers. User declarations are not live quotes, complete 1h series, attributable income or qualified policy inputs; public feeds/financial rows remain disabled.

**Credit core:** P-07a adds a [durable exact posted-credit ledger](../credit-ledger.md) for synthetic-source qualification, with revision/provenance/coverage recovery and pure latest-price references. No real provider, background collection, calendar forecast or financial dashboard activation is supplied.

**Credit ownership:** P-07b adds [bounded isolated local ledger jobs](../credit-owner.md) and immutable generation-bound results with explicit lost-acknowledgement reconciliation. It does not qualify a real provider, persist pending jobs, supply recovery/collection UI or enable production financial rows.

**Credit restart recovery:** P-07c adds [durable pending intent/receipt and exact restart reconciliation](../credit-recovery.md) to the isolated owner by default. Inspection never auto-applies; confirmed receipts require read-only latest-ledger proof and failed retries preserve old uncertainty. Real-source/collection cursor, native recovery/import/export UI, parent-death/OS-stall packaging and financial activation remain unfinished.

**Electricity/time core:** P-05a/P-08a adds [optional offline IANA civil time, exact sample-and-hold energy and supported declared tariffs](../electricity-core.md), including pure full-remainder whole-meter counterfactuals. No real plan certification, telemetry/settings activation, income/net forecast or optimizer is supplied.

**Native energy defaults:** P-05b/P-08b adds [reviewed flat energy-only settings and optional session GPU-board estimates](../session-energy.md), an explicit power-read validity flag, bounded background accumulation and `--no-energy`. Settings freeze until the next explicit session. It is not a combined utility-bill adapter, persistent energy history, whole-PC/incremental measurement, income/net forecast or production hardware qualification.

**Native credit review:** P-07d adds [private-original-input/status review and revision-bound exact retry](../credit-review.md) under Privacy. Ordinary settings opens no credit store/collector and clearly reports unavailable; the test-only fixture exercises the real helper. Tickets are not durable cursors, and changed review, lost reply or failed retry cannot erase uncertainty or enable income.

**Atomic source checkpoints:** P-07e adds [fixture cursor persistence and one-page admission](../credit-cursor.md). Explicit v2 streams commit facts and cursors together, retain exact pending input and disclose committed checkpoints separately. No v1 migration, real provider/fetcher or production financial activation is implied.

**Observed energy history:** P-05d/P-08c adds [bounded raw-power history and explicit independent reset windows](../energy-history.md), exact coverage, cross-session identity and isolated atomic storage/recovery. P-05e/P-08d adds [durable pending originals/receipts](../energy-recovery.md). P-05f/P-08e adds [opt-in exact checkpoint compaction](../energy-compaction.md). P-08f adds a [bounded producer, private selected-device mapping and native recovery seam](../energy-capture.md). P-08g adds [isolated saved consent, private identity enrollment and injected native controls](../energy-authorization.md). Whole-collector admission/lifecycle, ordinary-runtime binding/packaging, real-cadence retention qualification, monetary billing state and production activation remain unfinished.

**Book validation:** four documentation-contract test cases pass. The final Windows selection passed 15/15 tests (14 product regressions plus the book contract); Linux focused display/settings checks and real PTY/fake-supervisor scenarios also passed. These are host/document checks, not production CUDA, game or live-income qualification.

## Read in this order

1. [Requirements and traceability](requirements.md): stable requirement IDs, modules, tasks, tests and status.
2. [System and module boundaries](system.md): ownership, state transitions, interfaces and dependency rules.
3. [Data and calculation contracts](data-and-calculations.md): accepted counts, income, valuations, forecasts, electricity and time.
4. [Terminal settings and dashboard](terminal-experience.md): pages, fields, visibility, navigation and activation rules.
5. [Test and release strategy](testing.md): deterministic acceptance cases and evidence gates.
6. [Execution plan and handoff](execution-plan.md): dependency order, small contribution batches and resumption checklist.
7. [Decision register](decisions.md): settled choices, superseded alternatives and unresolved implementation choices.
8. [中文阅读入口](README.zh-CN.md).

Related evidence:

- [International electricity-billing research](../research/international-electricity-billing.md): ten jurisdictions, primary-source citations, capability limits and 34 synthetic tariff test specifications. These are researched structures, not shipped tariff presets.
- [Market/credit provider research](../research/market-and-credit-providers.md): explicit Gate Pearl/Kraken FX identity and schema limits; the reviewed Kryptex contract is insufficient for attributable actual-credit rows.
- [Initial feature roadmap](../feature-roadmap.md) and [first-slice host validation](../settings-validation.md).
- Upstream [CONTRIBUTING.md](../../CONTRIBUTING.md) and [SECURITY.md](../../SECURITY.md) remain authoritative for contributions and vulnerability reporting.

## Product objective

An explicitly started miner can optionally choose the best measured, safe mining profile for estimated net return per elapsed time. It accounts for applicable electricity costs, fresh market/reward information and optional game-experience constraints. The user controls these features through CLI/config and the native terminal settings menu. The system never guarantees market returns or silently starts mining as a side effect of setup.

The native dashboard also reports durable accepted-share history, market information, today's revenue forecast, actual credited revenue, its valuation and optional estimated net profit. A counter, forecast, account credit, token balance, payout and sale are different facts.

## Current implementation truth

| Capability | Status: baseline plus display/history batches |
|---|---|
| S-entry settings, navigation, review/save/discard and idle settings command | Implemented; host/PTY evidence linked above |
| Display refresh, terminal color, recent events and hiding worker label | Implemented, local presentation preferences only |
| Durable 24h/7d/30d/all accepted counters | Implemented for observed user-share counters; partial coverage and clock/recovery warnings explicit |
| Market adapters and native market projection | Offline-tested exact market core/owner/projection; not production-activated or live-qualified |
| Manual/offline reference input and terminal display | Implemented default-off native subpage and separate preferences; user-declared PRL/USDT and FX only, not income/automation |
| Exact posted-credit storage/valuation core | Implemented and tested for synthetic contracts only; no real source or runtime activation |
| Actual-credit adapters and financial income dashboard | Not implemented |
| Configurable Details/Log pages and fields | Implemented after the baseline; see display settings source/tests |
| Declared combined bill/time arithmetic | Offline-tested pure core and native combined-rule/declared-opening settings; no certified supplier adapter, measured persistent billing/reset state or runtime activation |
| Native flat-energy defaults and session board energy | Default-off native review/save and bounded session-only model; typed power validity/coverage, not whole-PC/incremental energy or net income |
| Persistent observed power and independent reset windows | Bounded audited core/helper/owner implemented; explicit windows only, no native persistence activation, long-run compaction or monetary bill state |
| Automatic profitability selection | Not implemented |
| Tested game pause/coexist policies | CPU-tested episode/profit guard, native detector, cooperative pause and optional lifetime supervisor bridge; ordinary activation and real experience/economic qualification remain open |
| Stratum TLS | Not implemented |
| First internationalization language packs | Not implemented |

No personal prototype, local GPU curve, wallet observation or machine-specific deployment is evidence that a fork feature is implemented or universally safe. Disabled groups must stay explicitly unavailable until their backend and tests exist.

The display batch adds a tested **read-only financial presentation contract**, not a live data provider or durable income ledger. Its default runtime state stays disabled until an integrated provider supplies validated data. Real-time market, actual income, forecasts and tariff computation remain unimplemented integrations. Accepted-counter history is now separate and must never supply fabricated token income.

## Document authority and maintenance

- `requirements.md` is the product contract. Settled semantics live in `decisions.md` and `data-and-calculations.md`; explanatory UI copy cannot silently change them.
- Statuses are `DONE`, `PARTIAL`, `PLANNED` or `BLOCKED`. Only executed evidence permits `DONE`. The book may contain designs for `PLANNED` modules.
- Every feature task references requirement and test IDs. Update implementation status, evidence, migration notes and open questions in the same contribution that changes behaviour.
- A changed requirement gets a decision entry and revised tests; do not erase the reason for a previous choice or relabel an old measurement as a new test.
- After each batch, leave a resumable ledger: branch/commit, completed tasks, exact validation scope, remaining blockers and next task. Never include personal paths, wallets, tariffs or raw runtime logs in that ledger.
- `tests/test_architecture_contract.py` checks the document cross-references, statuses, locales and key calculation invariants. It does **not** prove runtime financial or mining correctness.

## Scope limits

Pearl and currently supported NVIDIA architectures remain the mining scope. This roadmap does not promise other coins, arbitrary GPUs, HiveOS, macOS, new kernels, an exchange/trading client or a separate graphical application. Windows/Linux share the portable core; hardware/game capability support is reported per platform. No country-wide tariff catalogue or complete worldwide language coverage is claimed.
