# Kryptex Pearl credit API and export guidance

**Evidence reviewed:** 2026-10-30. **Status:** public-documentation research; no live credit-source qualification.

**Conclusion:** Kryptex publishes reward-chart JSON and a payout-history CSV endpoint. The current published contract still does **not** qualify attributable posted PRL credits for this project's exact rolling-24h/cumulative actual-income ledger. An export can clarify a file's format, but cannot supply missing provider guarantees.[^schema]

This follows [provider research §7](market-and-credit-providers.md#7-kryptex-actual-credit-capability-assessment), [data contracts §4](../architecture/data-and-calculations.md#4-actual-income-and-latest-price-valuation--final-decision) and the [posted-credit ledger contract](../credit-ledger.md). Research used agent-reach's public-page/Jina route and a direct download of the official schema. Only public documentation and its published illustration assets were retrieved. No wallet/account/miner endpoint, login, private sample, mining connection or support submission was used. Downloaded sources remain outside the repository; no documentation screenshot or financial figure is reproduced here.

## 1. Schema comparison and documented exports

The [official API page](https://pool.kryptex.com/api) still links `https://pool.kryptex.com/openapi.yaml`, **OpenAPI 3.0.3**, API version **`1`**.[^api] SHA-256 of the directly downloaded raw bytes:

```text
70072837d1f7120ae1fdffb289b8ce0716bf4d567cbd4ddc2cba3128186ce3db
```

This exactly matches the **2026-10-28** snapshot in the earlier note: **no schema change was detected**. The public guides reviewed below add navigation and payout explanations, not a qualifying per-credit API contract. This is a finding about the cited public evidence, not proof that no other vendor interface exists.

All paths below are **documented templates, not executed requests**. The schema uses generic `{coin}`; the official Pearl page uses `prl`. PRL-specific response behavior has not been live-verified.[^schema][^prl]

| Published GET path | What the schema actually supplies | Qualification limit |
| --- | --- | --- |
| `/{coin}/api/v1/miner/reward-chart/{address}` | “31-day reward chart”; JSON array of `RewardPoint { date: integer, reward: number }`. Only coin/address parameters. | Address-scoped dated rewards, not documented individual posted credits. No worker, credit ID, revision, posted timestamp, status or completeness/finality declaration. Date units, bucket bounds/timezone and amount scale/net basis are unspecified. |
| `/{coin}/api/v1/miner/payouts/{address}/csv` | “Payout history as CSV”; response **`text/csv`, schema `string`**. Only coin/address parameters. | A documented payout export, not a credit export. **CSV headers/order, delimiter, date format, precision and retention are not specified.** |
| `/{coin}/api/v1/miner/payouts/{address}` | Paginated JSON `count/results`; `Payout { date, received, txid, status }`; optional `page`. | These four fields belong to the **JSON** schema; they are not verified CSV columns. Transaction IDs identify payouts, not earning credits. |
| `/{coin}/api/v1/miner/payouts/{address}/stats` | `paid`, `unpaid`, `reward.week`, `reward.month`. | Totals lack individual credit identity, posting times and attribution. |
| `/{coin}/api/v1/miner/chart/{address}/{worker}`; same path with `/{scheme}` | Worker history: `timestamp`, `hashrate`, `shares`; optional `days`. | Work history, not token-credit history. |
| `/{coin}/api/v3/miner/workers/{address}` | Worker names, scheme, hashrate, shares, last share. | Worker activity does not attribute wallet rewards to a worker. |
| `/{coin}/api/v1/miner/balance/{address}`; `/{coin}/api/v1/miner/blocks/{address}` | Balance stocks; found-block records including worker/scheme. | Neither is a general posted-credit ledger or revision stream. |

The table is a schema inventory, not a sampled response.[^schema] In particular, a payout CSV is useful for payout bookkeeping; calling it an “income report” does not change its accounting meaning.

## 2. Why strict actual income remains unavailable

The current [ledger](../credit-ledger.md) requires stable provider credit IDs, ordered replacement revisions/explicit adjustments, exact net token amounts and scale, **posted UTC milliseconds**, scope/generation, source snapshot/as-of and explicit completeness intervals. Its optional [cursor contract](../credit-cursor.md) additionally requires a repeatable consecutive revision stream; payout pagination does not establish that contract.

The reviewed reward/payout interfaces do not document those facts.[^schema] Consequently:

* Neither a local hash of `(date,reward)` nor a payout `txid` can become a provider credit ID. A chart edit must not create a second earning event.
* Even confirmed daily buckets could not reconstruct an arbitrary exact rolling 24h boundary. “31-day” does not establish all-time history or gap-free coverage; empty data does not establish zero income.
* Prorating wallet rewards by worker hashrate/shares would be an estimate. One visible worker does not establish historical exclusivity. Explicit account scope would still leave identity/time/revision/coverage gaps.
* The FAQ says PPS+ balances are confirmed immediately, but does not define reward-chart posting times, immutable records or revision/finality guarantees.[^faq] Balance confirmation is not a substitute for the ledger contract.

**Result:** actual-credit support remains unsupported by the reviewed evidence. A separately labelled wallet reward aggregate could be considered only after its unit, time, scope and revision semantics are established. A synthetic fixture must never be presented as a Kryptex earning sample.

## 3. Wallet PRL versus account-converted BTC

Kryptex's Pearl page supports **wallet/worker** and **username/worker** modes. Its account-mining guide explicitly includes PRL and says all earnings from auto-convert mining are **credited and stored in Bitcoin** in the Kryptex account; withdrawal options include other assets.[^prl][^account]

* **Direct PRL wallet:** pool statistics/payouts concern the selected Pearl wallet. These are still wallet-scoped, not automatically local-worker credits.
* **Kryptex account / auto-convert:** the documented account location is **Pool Workers**. A BTC account earning or a USDT withdrawal does not establish a pre-conversion PRL credit quantity. Do not reverse-convert BTC at a market quote and call it mined PRL. The reviewed public guide supplies no pre-conversion PRL credit export/API contract.[^account]

The useful first distinction is simply **wallet/PRL** versus **account/auto-converted BTC**. Actual addresses, usernames and credentials are unnecessary to explain the integration gap.

## 4. Established, low-effort user steps

These are instructions for the owner to use locally; no private destination needs to be shared with an assistant or page-reading service.

A mining connection such as `stratum+tcp://POOL_HOST:PORT` is not a website address. Use the HTTPS Pearl page below to view statistics. A worker label alone does not identify the wallet to search. The query must use the exact PRL payout address configured for that miner; a different receive address in the same wallet does not establish the same pool-statistics scope. These navigation checks do not establish whether mining or crediting succeeded.

### Direct-wallet statistics and payout history

1. Open [the Pearl pool page](https://pool.kryptex.com/prl).
2. Copy the exact existing PRL payout address from the miner's configuration or its matching entry in the owner's Pearl wallet. Paste the address **without a worker suffix** into the page's **search field**, not the browser address bar, and press **Enter**. Kryptex's Pearl guide explicitly documents this navigation to wallet/rig statistics.[^pearl-guide] No mining-configuration edit is needed to search.
3. Open the **Payouts** tab. This is the documented place to view payout history.[^pearl-guide] The payout FAQ's answer to “How do I get a daily earning report for the last month?” points here and describes **payouts**, not per-credit earnings.[^payout-guide]

**No “Export earnings” or “Download CSV” website button was established by the reviewed guides.** They establish the Payouts tab; the OpenAPI establishes a CSV route. Do not invent a button sequence or ask someone to change payout settings to obtain evidence.

### Optional local download via the documented API

For an owner who specifically needs a payout file, the published CSV path specializes to this **unvisited template**:

```text
https://pool.kryptex.com/prl/api/v1/miner/payouts/{address}/csv
```

Replace `{address}` locally with the wallet address only, without a worker suffix, and open it in the owner's browser. If the response displays rather than downloads, the browser's own **Save As** can save the response. This is an API-based download recipe, not a verified website export button; successful PRL retrieval, file naming and CSV layout remain untested.[^schema]

For format investigation only, the documented reward JSON template is:

```text
https://pool.kryptex.com/prl/api/v1/miner/reward-chart/{address}
```

It supplies the chart shape described above, **not** a qualified credit-ledger export. No extra date, worker or pagination parameters are documented for it. Neither template should be populated with an account username/email on the assumption that account auto-convert data is interchangeable with wallet data.[^schema][^account]

### Account mode

The official guide directs account miners to **Pool Workers** in their Kryptex account.[^account] The public documentation reviewed does not establish a PRL-credit export button there. A public vendor explanation of any pre-conversion export is the next useful evidence; account access is not needed for this research.

## 5. Useful anonymized evidence

For a future format review, the lowest-effort useful evidence is **column/field names and unit/time/status labels**, rather than a full account export. No private sample is needed to reach this note's conclusion.

* A header-only CSV or a fully synthetic few-row reproduction can establish structure. Preserve names, value types, null/missing distinctions, decimal precision and timestamp format. Mark all invented values as synthetic; never add fields the provider did not supply.
* A cropped/redacted view can establish the exact tab/export-control wording, asset label and relevant tooltip text. Remove addresses, account/email/worker names, transaction links/IDs, amounts, dates and browser chrome that identifies the owner.
* If examining a documented revision example, consistently replace repeated identifiers with placeholders such as `credit-A` and `worker-A`, preserving relationships. Structural anonymization does not prove provider identity, coverage or finality.
* Public API/documentation links or a vendor-authored synthetic example are preferable evidence for semantics. Keep raw exports, private URLs, request headers, HAR files, cookies and credentials out of repository contributions.

## 6. Questions only the vendor can resolve

1. Is there a supported PRL **posted-credit** export/API, distinct from reward charts and payouts, for direct-wallet and/or account-auto-convert mining? Are pre-conversion PRL quantities retained for account mining?
2. Does it provide stable credit IDs, ordered revisions and correction references; exact posted times/units; net quantities/precision; worker versus wallet scope and historical attribution?
3. What exactly are reward-chart `date`/`reward`: bucket duration/timezone/bounds, earning versus posting basis, fees, partial buckets, late corrections and PPS+/SOLO scope?
4. What proves complete coverage and finality, including a genuinely empty interval? What are history limits, pagination/snapshot guarantees and replay behavior?
5. What are the CSV headers/format, supported PRL export steps, rate/cache limits and permitted local storage/display use? The schema does not answer these; its index endpoint's three-minute HTTP cache is not a reward-endpoint guarantee.[^schema]

### Optional support-query draft — not sent

> Does Kryptex Pool offer a documented Pearl/PRL posted-earning ledger API or export, separate from the 31-day reward chart and payout CSV? We need stable credit IDs, revisions/corrections, exact posting times, net PRL amounts/precision, worker or wallet scope, and complete-history/finality semantics. For account auto-convert mining, is pre-conversion PRL available separately from credited BTC? Please provide public documentation or a synthetic example, the exact export steps and CSV headers, plus retention, pagination and rate limits. No account-specific investigation is requested.

The official Pearl guide lists support contact methods; none was used.[^pearl-guide]

## Sources

First-party sources reviewed on 2026-10-30; endpoint names below identify schema documentation, not calls to those endpoints.

[^api]: Kryptex, [Kryptex Pool API](https://pool.kryptex.com/api), public API description, base URL and “Download openapi.yaml” link.
[^schema]: Kryptex, [OpenAPI schema](https://pool.kryptex.com/openapi.yaml), `info.version`, `paths` for reward-chart, payouts/CSV/stats, worker charts/workers, balance and blocks; `components.schemas.RewardPoint`, `Payout`, `PayoutStats`, `WorkerChartPoint`, `Worker`, `Balance`, `MinerBlock`; `components.parameters`. CSV response is only `text/csv` / `type: string`. Raw SHA-256 is recorded in §1.
[^prl]: Kryptex, [Pearl Mining Pool](https://pool.kryptex.com/prl), wallet/username formats, Wallet/Kryptex account selection and “Mine PRL, receive USDT” explanation. Dynamic statistics and financial figures are not evidence for this note.
[^pearl-guide]: Kryptex, [How to mine Pearl (PRL)](https://pool.kryptex.com/articles/how-to-mine-pearl-en), “Monitoring Your Pool Mining Activity” steps 1–3 (Pearl page, wallet search, Enter), “Payouts” step 6 (Payouts tab), and “Need Help.” Published illustrations were inspected only as documentation, not live wallet pages.
[^payout-guide]: Kryptex, [How to make a payout?](https://pool.kryptex.com/articles/about-payouts-en), “How do I get a daily earning report for the last month?”: directs readers to payouts in the Payouts section; it does not specify an earnings CSV button or credit-ledger fields.
[^account]: Kryptex, [Mine Anything — Get Bitcoin!](https://pool.kryptex.com/articles/account-mining-en), introduction, supported-coins table (PRL), “Quick Guide for Experienced Miners” / “Connect on Windows” (Pool Workers) and “Important Information” (earnings stored in BTC). Linked account pages were not opened.
[^faq]: Kryptex, [Common questions](https://pool.kryptex.com/articles/how-pool-works-en), “What Is an Unconfirmed Balance?”: PPS+ balance immediately confirmed; no per-record reward-chart finality contract.
