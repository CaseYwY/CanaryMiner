# International electricity billing: tariff-engine research

**Evidence retrieved:** 2026-10-28. **Purpose:** design guidance for CanaryMiner, not a production tariff catalogue, bill quotation, legal eligibility determination, or statement of implemented features.

This note examines ten jurisdictions across six inhabited continents. Examples describe named plans, utilities, service territories, or regulatory frameworks—not national default tariffs. Monetary prices are intentionally omitted. Numerical prices in the test section are entirely synthetic. Thresholds, intervals, and dates elsewhere illustrate rule structures, not recommended settings.

**Evidence status:** official utility/regulator documents and first-party utility documentation only; no third-party calculators or blogs are used as evidence. “Verified” below means the stated structure was visible in retrieved primary-source text, not that every account can use it or that no subsequent amendment exists. The Kerala PDF findings have a specific retrieval limitation identified below. Engineering proposals and mathematical deductions are explicitly distinguished from source facts.

## 1. Executive findings

1. **Use a composable, stateful bill model, not a country-to-price lookup.** Progressive blocks, all-units bands, local time schedules, event-day calendars, and dynamic intervals are materially different primitives. PG&E E-1, Kerala domestic billing, EDF Tempo, and Octopus Agile demonstrate these differences.[^us1][^in1][^fr1][^gb2]
2. **A mining decision needs the change in the whole-meter bill.** For nonlinear tariffs, applying the household’s average price or independently pricing each miner produces the wrong counterfactual. This is a mathematical consequence of blocks, maximum-demand charges, and export credits.[^us1][^us2][^ca2]
3. **Purchased energy and consumed energy need separate ledgers.** Cape Town’s prepaid blocks accumulate purchases within a calendar month; a consumption-only model cannot reproduce token acquisition cost.[^za2]
4. **Capacity and demand are not interchangeable with kWh.** France has subscriptions differentiated by contracted kVA; PG&E has interval-maximum demand components; Eskom also defines rolling annual utilised capacity. Do not spread these into a supposedly exact instantaneous energy price.[^fr1][^us2][^za1]
5. **Negative wholesale prices are not automatically negative retail bills.** Agile explicitly passes through negative-price opportunities, but still has a standing charge. Brazil’s monthly generation-cost flags and Japan’s fuel adjustments are different mechanisms, not live wholesale-price tariffs.[^gb2][^br2][^jp2]
6. **Billing equivalence requires plan-specific evidence and local state.** The reviewed sources distinguish bundled versus unbundled charges, tax-inclusive versus tax-exclusive prices, time bases, and credit rules. Missing information must reduce the confidence label rather than silently become zero.[^us1][^fr1][^au2][^ca2]

## 2. Identity: do not infer a tariff from language or geography

**Proposed configuration contract:** keep the following independently selectable and editable. This is a product-design recommendation, motivated by territory-dependent PG&E allowances, GB regional/payment distinctions, French plan options, and Ontario utility-versus-retailer distinctions.[^us1][^gb1][^fr1][^ca1]

| Field | Meaning and boundary |
| --- | --- |
| UI language | Translation preference only; it must not select a tariff or imply residence. |
| Formatting locale | Decimal/group separators, date presentation, and display conventions; separate from calculation. |
| Country/jurisdiction | Regulatory context and optional catalogue filter, not a price. Include state/province where relevant. |
| Currency | Explicit currency code and subunit scale; never infer from a symbol or UI language. Keep optional exchange-rate conversion outside the bill calculation. |
| Utility/supplier | The contracting seller; distinguish it from distributor/network operator and a separate generation provider. |
| Service territory | The actual network and tariff zone, possibly including a baseline/climate sub-territory. |
| Plan and revision | Exact tariff code, option, contract vintage, effective dates, payment method, eligible load class, and grandfathered status. |
| Meter arrangement | Import/export registers, general versus dedicated circuits, interval length, supply capacity, and prepaid/postpaid settlement. |
| Billing clock | Explicit named time zone, civil-time versus fixed-standard-time convention, billing boundaries, and tariff-day definition. |

Do not automatically geolocate, read account identifiers, or select a residential tariff because mining happens in a dwelling. Tariff/load eligibility and permission to operate remain for the customer and relevant authorities to establish. This research does not assess mining legality in any jurisdiction.

## 3. Compact provider/tariff capability matrix

This is an **evidence/capability matrix for proposed adapters**, not a list of supported CanaryMiner integrations. “Not established” is not evidence that a feature is unavailable. Capabilities in different plans in a row must not be merged into an invented hybrid plan.

| Jurisdiction and representative scope | Energy structure evidenced | Fixed, demand, or capacity structure | State/reset and import/export boundary | Adapter gate |
| --- | --- | --- | --- | --- |
| US — California, PG&E E-1 / B-19 / solar arrangements | E-1 progressive baseline blocks; B-19 seasonal TOU | E-1 daily base-services charge; B-19 multiple demand maxima | Billing-cycle/daily seasonal allowances; solar programme and true-up separate | Identify exact schedule, service bundle and solar vintage.[^us1][^us2][^us3][^us4] |
| UK — Great Britain, default tariffs / Octopus Agile / SEG | Fixed or variable unit prices; Agile published half-hourly dynamic prices, including negative events | Daily standing charge | Interval price validity; SEG separately meters exports | GB evidence does not establish Northern Ireland rules; select actual regional/product version.[^gb1][^gb2][^gb3][^gb4] |
| France — continental metropolitan Tarif Bleu options | Base; locally assigned peak/off-peak; Tempo colour × time periods | Subscription depends on contracted kVA | Supplier announces Tempo day colour; import and self-consumption arrangements differ | Contract option, local schedule, eligibility and HT/TTC basis required.[^fr1][^fr2] |
| Australia — retailer tariffs; Victoria United Energy network examples | Single rate, TOU, dedicated controlled load | Retail demand methods vary; United Energy monthly 30-minute small/medium-business maximum | Local-time/DST convention, with specified exceptions; dedicated circuit excluded from general-load demand in stated combinations | Network charges are not automatically extra retail charges.[^au1][^au2] |
| China — Zhejiang residential / commercial policy; Jilin historical annual rules | Zhejiang annual residential bands × peak/valley prices; commercial seasonal/holiday periods | Commercial price components and separate capacity/demand categories | Jilin illustrates monthly provisional billing and annual reconciliation | Historical residential structure needs current local confirmation; market participants follow their own market rules.[^cn1][^cn2][^cn3] |
| India — Kerala, KSEB domestic schedule | Telescopic up to a monthly threshold; non-telescopic/all-units above it; separate ToD provisions | Consumption/phase-sensitive fixed charges; other classes have demand charges | Monthly versus bi-monthly billing and revision apportionment | PDF findings are primary-index-text verified, not full-PDF verified; require authoritative schedule before activation.[^in1][^in2][^in3] |
| Japan — TEPCO Energy Partner, Meter-Rate Lighting B/C, Kanto adjustment example | Progressive energy blocks plus monthly fuel adjustment and renewable surcharge | B basic fee by contracted amperage; C by kVA | Monthly pricing; an announced revision can need old/new proration | Do not substitute another TEPCO product or region’s adjustment.[^jp1][^jp2] |
| Brazil — ANEEL Group B conventional / Tarifa Branca / SCEE | Conventional single-rate versus weekday TOU; monthly tariff flags | Availability-cost floor; contracted demand remains relevant for Group A | Flags have month-specific validity; generation compensation is a separate ledger | Distributor, tax treatment, connection and compensation regime required.[^br1][^br2][^br3][^br4] |
| South Africa — Eskom direct / Cape Town municipal | Eskom Homepower single energy rate, Homeflex TOU; Cape Town prepaid progressive blocks | Eskom capacity/service components and large-load demand; municipal fixed charges differ | Eskom annual capacity history; Cape Town calendar-month purchases and accrued daily fees | Never apply municipal prepaid blocks to Eskom direct supply by default.[^za1][^za2] |
| Canada — Ontario RPP and net metering | Alternative TOU, Ultra-Low Overnight, or progressive monthly tiered plan | Delivery and other bill components distinct from electricity-line prices | Seasonal tier thresholds/TOU calendars; restricted export credits, up to 12-month carryover | Retailer contract prices and Global Adjustment treatment differ from RPP.[^ca1][^ca2] |

## 4. Jurisdiction findings and implementation consequences

### 4.1 United States: California is still not a single tariff

**Verified:** PG&E E-1 defines baseline quantities per day by baseline territory, season, and service characteristics. Its seasonal-change rule adds each season’s daily allowance multiplied by the applicable days in the billing period. Energy within and above baseline has separate prices. The retrieved schedule also has a daily base-services charge and distinguishes bundled, direct-access, and community-choice generation service.[^us1]

**Consequence:** keep allowance accrual separate from energy accumulation. A month boundary does not itself imply an E-1 reset; use the account’s billing cycle. Do not add a quoted bundled energy rate to its own unbundled components, or use the bundled generation component when another supplier provides generation. Account assistance/allowance status can be entered as a local rule selection without storing evidence of income or health.[^us1]

**Verified:** B-19 defines maximum demand using interval averages, normally 15 minutes, with specified exceptions. Its basic structure has maximum, peak-period and part-peak-period demand components; the bill can include all of them. It also specifies seasonal split treatment and variations such as daily demand components under Option S.[^us2]

**Consequence:** “one monthly peak” is not a sufficient universal demand model. A miner outside one demand window may still increase another demand component. This does not mean all components in all B-19 options should be added together: select one complete option and its applicable rules.[^us2]

**Verified:** legacy NEM documentation describes monthly readings and a 12-billing-cycle true-up. The Solar Billing Plan distinguishes imports, export credits, other charges, and annual true-up; its business explanation says credits do not offset demand and non-bypassable charges.[^us3][^us4] **Boundary:** these findings do not specify a complete NEM/NBT settlement adapter. Programme, vintage, credit eligibility and reconciliation need the applicable legal tariff, not a generic “net metering” switch.

### 4.2 United Kingdom: GB unit charges, dynamic imports, separate exports

**Verified:** Ofgem describes daily standing charges payable even on days with no energy use. Its price-cap explanation distinguishes region, meter/payment arrangements and standard-variable versus fixed tariffs; the cap is not a limit on a customer’s total bill.[^gb1]

**Verified:** Agile uses half-hourly retail prices updated daily and smart-meter half-hourly consumption; it explicitly allows customers to be paid for consumption during negative-price events. A daily standing charge remains. Octopus’s documentation exposes time-bounded rate records and separate tax-inclusive/exclusive values, distinguishes regional/product codes, and documents UTC/DST and consumption rounding behaviour.[^gb2][^gb3]

**Consequence:** prefer the exact published retail price stream to recreating a tariff from generic wholesale prices or an old formula. Never clamp a verified signed unit price to zero. Missing future prices are unknown, not free; current product-specific caps and tax rules must be obtained from the actual contract. The retrieved marketing page contains historical discussion, so it is evidence of structure, not a reliable source for every current parameter.[^gb2][^gb3]

**Verified:** SEG is a Great Britain export-payment framework. Licensees choose contract terms, but SEG tariff rates must be above zero and payments use export meter readings.[^gb4] **Consequence:** negative Agile import prices do not imply negative SEG export prices, nor does a SEG credit automatically net against each import interval. Northern Ireland was not researched here.

### 4.3 France: subscription capacity and notified day types

**Verified:** the CRE July 2026 annex for continental metropolitan residential Tarif Bleu separates annual subscription/capacity terms and energy periods. Base has one period; Heures Creuses has two with schedules set locally by the network operator; Tempo has six combinations of day colour and peak/off-peak. The annex describes 22 red days, 43 white days, and 300 or 301 blue days, with next-day colour notified by the supplier. It also specifies restrictions/retirement for certain Base capacities.[^fr1]

**Consequence:** do not assume the same off-peak hours for every French account. Tempo needs an authoritative day-type input and the supplier’s tariff-day boundary; a colour quota is not a forecast of which future date is red. The full operational day-colour boundary/announcement interface was **not verified** here; do not silently use civil midnight. Distinguish a capacity subscription from an interval-maximum demand charge.[^fr1]

**Verified:** the annex’s grids are tax-exclusive and already include specified standard network-service components. CRE explains that tax-inclusive TRVE also includes CTA, electricity excise, and VAT.[^fr1][^fr2] **Consequence:** tax metadata must identify the taxable components and dependencies; adding a second network charge or taxes already included in a TTC quote would double-count. Continental metropolitan findings must not be projected onto non-interconnected territories, which the annex treats separately.[^fr1]

### 4.4 Australia: network versus retail, dedicated circuits, and clock conventions

**Verified:** the Australian Energy Regulator’s Energy Made Easy explains single-rate, TOU, controlled-load and demand tariffs. Controlled load is tied to particular appliances/circuits; demand plans can charge a maximum, an average of peaks, or seasonally different demand rates. Meter configuration can restrict access to plans.[^au1]

**Verified:** United Energy’s 2026–27 Victorian network FAQ specifies 30-minute kW demand for small/medium businesses, monthly maxima with monthly reset rather than a rolling 12-month rule. Large-business demand is measured as kVA at maximum kW. TOU/demand measurements follow local time and DST except the stated Type 7/9 metering exception. It also excludes the dedicated circuit from general-circuit demand in the specified combinations.[^au2]

**Consequence:** model the retailer’s actual bill, not a distributor’s tariff added to a retail quote that already recovers it. The network FAQ does not establish a retail customer’s contractual pass-through. Do not charge a miner at a hot-water controlled-load price unless its actual authorised circuit/meter arrangement qualifies. Clock basis, demand units, and demand windows belong to the particular plan—not “Australia”.[^au1][^au2]

### 4.5 China: annual blocks can coexist with TOU; commercial rules change

**Verified historical structure:** Zhejiang’s 2023 provincial annex lists annual residential energy bands and peak/valley prices within those bands, while identifying collective-meter arrangements separately. This is evidence that tiering and TOU can be combined, not that this document alone certifies every current account’s tariff.[^cn1]

**Verified:** Zhejiang’s May 2026 commercial/industrial notice, effective 2026-07-01, has seasonal periods and special deep-valley holiday windows. Its TOU multiplier base comprises supply, upstream losses and system-operation components, while transmission/distribution and government funds/additions do not participate in that multiplier. Direct market participants follow market rules. The notice explicitly repeals the 2024 notice.[^cn2]

**Consequence:** store component-specific formulas and effective-dated calendars. Multiplying the complete delivered rate by a peak factor would be wrong for the cited 2026 rule. Do not reuse the repealed 2024 commercial schedule or use a residential annual-band table for an industrial account.[^cn1][^cn2]

**Verified historical reset example:** Jilin’s 2012 notice, republished on its official disclosure site, describes annual tiering with monthly provisional settlement, annual reconciliation without cross-year settlement, and partial-year allowances based on months of service. It also distinguishes shared-meter users.[^cn3] **Unverified:** current applicability of this historical Jilin rule and detailed Zhejiang residential reset/move-in procedures. An adapter must obtain those details locally; “China resets monthly” and “all Chinese provinces reset identically” are both unjustified.

### 4.6 India: Kerala supplies an actual telescopic/all-units distinction

**Primary-index-text verified; full-document retrieval incomplete:** KSEB’s comparison schedule explicitly labels domestic energy charges “Telescopic tariff for monthly consumption up to 250 units” and “Non-telescopic tariff for monthly consumption above 250 units”. It also differentiates fixed charges and supply phase. Its official gazette has separate domestic ToD provisions, and a 2025 circular describes monthly/bi-monthly billing and revision-period apportionment.[^in1][^in2][^in3]

**Consequence:** non-telescopic/all-units selection is not another progressive block. Crossing the applicable consumption band can reprice earlier units and change the fixed-charge band. An optimizer must evaluate the full period counterfactual; the last unit’s displayed price is insufficient. Monthly-normalised thresholds must not be applied to an entire two-month bill without the approved conversion/proration rule.[^in1][^in3]

**Evidence limit:** direct PDF retrieval failed during this research; the quoted labels and revision distinctions were visible in search-index text of the utility-hosted originals. The official calculator independently exposes monthly, bi-monthly and actual-day modes and warns that actual bills vary by consumer status.[^in4] Exact boundary treatment, concession eligibility, current amendments, ToD rollout, and full bill-reproduction rules remain **unverified for production**. Do not ship a verified-current Kerala preset from these extracts alone. This limitation does not justify treating all Indian tariffs as progressive or all-units.

### 4.7 Japan: blocks plus adjustments, with effective-date proration

**Verified:** TEPCO’s monthly Meter-Rate Lighting B table distinguishes basic fees by contracted amperage and progressive energy stages: first 120 kWh, then through 300 kWh, then the excess. Lighting C has a basic fee per contracted kVA. The page labels amounts tax-inclusive and says fuel adjustments are added; it displays both pre- and post-2026-11-01 values.[^jp1]

**Verified:** TEPCO’s September 2026 notice separately specifies the forthcoming November fuel adjustment for Kanto low-voltage contract categories, includes renewable-surcharge/tax treatment in its explanatory model, and says actual payment across the November revision is a sum of prorated old and new amounts. It distinguishes consumption-month subsidies from billing-month labels and leaves some high-voltage adjustment values for later announcement.[^jp2]

**Consequence:** use signed adjustment components, explicit billing/application periods, subsidy validity and inclusion flags. A negative fuel adjustment is not proof that the total retail unit price is negative. Do not apply forthcoming November prices to October consumption just because the page already displays them. Detailed current fuel-adjustment caps, other products and exact meter-reading proration were **not fully researched**; these need plan-specific evidence.[^jp1][^jp2]

### 4.8 Brazil: TOU, monthly flags, minimum bills, and compensation

**Verified:** ANEEL describes conventional Group B pricing with one tariff value versus Tarifa Branca’s weekday peak, intermediate and off-peak periods; weekends and national holidays are off-peak. Eligibility exclusions apply, and hours/prices are distributor-specific. Monthly tariff flags signal generation costs and add a per-kWh amount when applicable; the cited explanation excludes isolated systems from that framework.[^br1][^br2]

**Verified:** ANEEL distinguishes energy/network/sectoral tariff components from federal PIS/COFINS, state ICMS, and municipal public-lighting contributions. Its distributed-generation overview describes SCEE credit balances, a 60-month credit validity, an availability-cost floor for Group B, and continued contracted-demand billing for Group A despite energy compensation.[^br3][^br4]

**Consequence:** neither a published energy tariff nor a national flag alone is a complete bill. Distinguish the availability minimum from an additional block of consumed energy; preserve the appropriate credit and connection-category rules. Current SCEE transition/grandfathering, compensation charges, tax bases/exemptions and local lighting contributions were **not exhaustively verified**; full Brazilian net-metering and tax settlement must remain unsupported until a regime-specific adapter is validated.[^br3][^br4]

### 4.9 South Africa: Eskom and municipalities must not be conflated

**Verified:** Eskom’s 2026–27 tariff book gives Homepower a single active-energy charge with capacity/service components and Homeflex seasonal TOU. Homelight is described with a single active-energy charge in this edition. It defines maximum demand over a 30-minute integrating period and annual utilised capacity from the higher of notified maximum demand and measured demand over a rolling 12 months. It also describes combining components in prepaid vending and Homeflex’s separately bounded Gen-offset credits.[^za1]

**Verified:** Cape Town’s 2026–27 municipal guidance explicitly excludes Eskom-supplied areas. It accumulates monthly prepaid purchases across transactions, resets blocks at the start of each calendar month, and recovers Domestic service/wires charges as a daily amount since the previous purchase. Eligibility assessment can use a previous-12-month average and property/meter criteria.[^za2]

**Consequence:** an older general explanation of Eskom inclining blocks is not enough to override the new tariff book. A municipal prepaid model needs purchase history, remaining prepaid inventory, fixed-charge accrual and any eligibility reassessment; a rolling capacity model needs demand history. The cost of consuming an already-paid token and the cost of eventually replacing it are different questions. Do not promise that changing purchase frequency within one month reduces block charges.[^za1][^za2]

### 4.10 Canada: Ontario plan choice and credit restrictions

**Verified:** Ontario RPP offers TOU, Ultra-Low Overnight (ULO), or Tiered pricing as alternatives. Tiered residential thresholds differ between summer and winter; TOU periods also change seasonally, while ULO periods are year-round. Holiday/weekend classifications affect time-based plans. These prices appear on the electricity line, distinct from delivery; RPP prices already include an estimate of Global Adjustment, whereas retailer-contract treatment differs.[^ca1]

**Verified:** OEB net-metering guidance limits credits to consumption-related charges, not every charge on the bill, with carryover for up to 12 months under the cited regulation.[^ca2]

**Consequence:** do not simultaneously apply the RPP TOU and Tiered plans, add Global Adjustment twice, or value all solar exports as unrestricted cash. Confirm actual billing-period threshold adjustment and utility delivery/tax treatment. The precise regulatory credit-expiry algorithm needs the regulation and utility agreement, not a blanket “every exported kWh expires exactly 365 days later”.[^ca1][^ca2]

## 5. Proposed rule and state model

The following is **engineering synthesis**, not a claim about existing CanaryMiner code. It follows from the contrasting primitives above.

### 5.1 Separate charge rules from mutable billing state

An immutable plan revision should describe:

- Currency and units: energy in kWh; demand in kW, kVA or another explicitly supported unit; daily/monthly/annual charge basis; currency subunits and exact decimal precision.
- Signed fixed energy rates, progressive bands, all-units selectors, minimum-charge floors, and any combination rules. Store whether bands are global, per TOU register, or shared across registers.
- TOU periods, seasons, named holidays, special events, tariff-day boundaries and an explicit precedence order. Invalid overlaps or uncovered intervals are configuration errors unless an intentional fallback exists.
- Dynamic retail-rate records with inclusive start/exclusive end instants, publication/revision timestamp, tax-inclusion flags and product/territory identity. A constant price may span many intervals; do not require one record per half hour.[^gb3]
- Component graph: sums, multipliers, selectors, floors/caps, credits and ordered taxes, including which inputs are already included in a bundled quote. Component names alone do not establish their unit or additivity.[^us1][^cn2][^za1]
- Demand rules: integration duration and alignment; eligible registers/windows; max versus top-N/average; contracted minimum; ratchet/lookback; kW/kVA distinction; daily versus monthly application; season-split/proration rules.[^us2][^au1][^au2][^za1]
- Independent import, export and credit rules: valuation intervals, allowed offsets, expiry/reset/true-up, payout restrictions and any non-bypassable charges.[^us3][^us4][^gb4][^ca2]

Local mutable state should hold only what the selected model needs:

- Period start/end and accumulated meter energy by applicable register; remaining allowances and year-to-date totals where required.
- Whole-meter baseline load/generation forecasts for counterfactuals; actual observations distinguished from forecasts.
- Demand maxima/lookback history and contracted capacity, not merely a miner’s latest watts.
- Prepaid purchases within the purchase period, prepaid inventory, accrued fixed fees and separately represented debt/deductions if supported.
- Export-credit balances with the regime’s reset/expiry state, and the applicable programme/vintage.

### 5.2 Progressive versus all-units calculation

For cumulative energy `q`, progressive energy cost is:

```text
progressive(q) = sum(rate[j] * clamp(q - lower[j], 0, upper[j] - lower[j]))
```

For an all-units rate selected by the completed period quantity:

```text
all_units(q) = q * selected_rate(q)
```

These are different functions; the latter can have discontinuities. If monthly average or a whole-period aggregate selects a rate, evaluate that selector after constructing the counterfactual period, including any prescribed proration. Do not retroactively reprice a progressive tariff or silently smooth a real all-units discontinuity.[^us1][^in1]

For combined annual tiering and TOU, the adapter must encode the documented allocation rule. Whether threshold crossings use chronological readings, a common surcharge, proportional allocation or another method cannot be inferred from a table containing both axes. Unknown allocation is **unsupported**, not “approximately exact”.[^cn1]

### 5.3 Shared-meter marginal cost

Let `B` be the complete bill/settlement evaluator, `L` the non-mining whole-meter trajectory, `M` proposed mining load, `G` behind-the-meter generation, and `S` the opening billing state. For a common evaluation horizon:

```text
incremental_mining_cost = B(L + M, G, S) - B(L, G, S)
```

Evaluate both branches from an identical copy of `S`; hypothetical evaluation must not mutate the live ledger. With several miners, evaluate the proposed joint schedule: independent “cost per miner” calculations are generally not additive. Fixed charges cancel only when they remain unchanged; a capacity upgrade, a minimum bill, a lost eligibility benefit, or a changed credit boundary may prevent cancellation. These are mathematical deductions from the cited charge structures.[^us1][^us2][^fr1][^in1][^za2][^ca2]

**Proposed horizon rule:** include the remainder of the affected billing/annual period and any persistent demand or credit consequences, or explicitly estimate the value of the ending ledgers. Otherwise a short horizon can falsely make prepaid inventory, unused cheap-tier allowance, or export credits appear free. A past bill difference and a forward scheduling estimate are different outputs: future non-mining consumption may use the remaining allowance or establish a new peak. Without a baseline forecast, report a declared standalone-energy scenario or range—not a verified shared-meter marginal cost.[^us1][^cn3][^za1][^za2][^ca2]

For a simple maximum-demand component with price `d` and eligible period `W`:

```text
incremental_demand_cost = d * (max_W(L + M) - max_W(L))
```

This formula only applies after the tariff’s averaging/alignment and any minimum/ratchet logic. Future household demand is uncertain; a peak already observed does not prove a miner cannot establish a higher one later. A kVA demand bill cannot be reconstructed exactly from active-power watts without the required apparent-power/power-factor information.[^us2][^au2][^za1]

With simple instantaneous net flow, a useful physical starting point is `import = max(load - generation, 0)` and `export = max(generation - load, 0)`. **Settlement is still plan-specific:** meter/phase aggregation, netting interval and gross-meter arrangements may differ. Mining can consume otherwise-exported energy and lose an export payment rather than buy imported energy; solar energy is not automatically free. Restricted or expiring credits must be evaluated through their ledger, not treated as unrestricted cash.[^gb4][^br4][^ca2]

### 5.4 Time, resets, precision, and causality

**Proposed implementation rules:**

1. Preserve interval instants in UTC and apply the plan’s explicit local civil or fixed-standard clock for classifications. UI locale and the computer’s current time zone are irrelevant to billing. Daylight-saving transitions can skip/repeat local clock labels; United Energy also documents a standard-time exception.[^gb3][^au2]
2. Use actual elapsed duration for energy integration. Count calendar days for a per-day fee when that is the tariff rule; do not equate a local billing day to exactly 24 elapsed hours.
3. Give energy blocks, purchase blocks, demand maxima, seasonal allowances, annual tiers, credit ledgers and true-up independent reset boundaries. Monthly invoices do not prove all those states reset monthly.[^us1][^us3][^cn3][^za1][^za2]
4. Split evaluation at plan revisions, TOU boundaries, reset instants, seasons, event transitions and tax changes. Apply the utility’s allocation/proration when readings do not resolve a boundary; label inferred allocation estimated.[^us1][^us2][^jp2][^in3]
5. Use exact decimal/fixed-point arithmetic with declared rounding stage and mode. Octopus documents rounding consumption to hundredths of a kWh using half-to-even before multiplication; this must not become a universal rounding rule.[^gb3]
6. Backtests may use only information published before each decision. Next-day prices and Tempo colours are not known for arbitrary future horizons. Distinguish publication time, validity time and later corrections.[^gb2][^fr1]
7. Distinguish a rate becoming effective from a block resetting. A mid-period rate revision must not replenish an allowance unless the tariff explicitly requires that.

## 6. What the mining optimizer may honestly report

These are **proposed capability labels and admission rules**, not an implementation audit.

| Label | Permitted result | Required local/user input or limitation |
| --- | --- | --- |
| Verified-rule calculation | Deterministic charge under an identified plan revision and specified input trace | All applicable rules, effective dates, clock, opening ledger state, taxes and rounding supplied/validated. It is not a guarantee that the supplier will not correct meter data. |
| Energy-only estimate | Miner energy × selected signed retail energy rates | Explicitly exclude fixed, demand, tax, credit or other unknown components. Do not call this total bill or exact profit. |
| Shared-meter forecast | Counterfactual bill difference for a proposed joint schedule | Baseline household/business load, generation and opening state; future parts are forecast, even if billing math is exact. Show assumptions and scenario range. |
| Demand-aware estimate | Incremental maximum-demand/capacity impact | Correct interval data, windows, contracted capacity, history and kW/kVA measurement. Missing future load or power factor must be surfaced. |
| Prepaid replacement estimate | Cost of the specified future purchase/replacement policy | Purchases, inventory, fixed-fee accrual and settlement rules. Show separately from cash already spent. |
| Export-aware estimate | Lost credits/payment and changed imports | Actual netting/credit rules, generation, balances, expiration and expected future use of credits. Do not use retail import price as universal export value. |
| Unsupported | No savings-driven schedule recommendation for the unknown charge model | Unresolved tariff identity, all-units selection/proration, credit allocation, reactive demand, subsidy eligibility, critical event, stale dynamic feed, or missing essential state. User may explicitly choose a clearly labelled simpler estimate. |

Local inputs include plan identity, service territory, currency, meter/supply arrangement, billing dates, opening energy/purchase/credit/demand state, verified rates/fees/taxes, and chosen forecast horizon. Public documents cannot establish a particular customer’s concessions, historical credits, tariff entitlement or account-specific contract. Runtime power measurement and mining-revenue assumptions are separate inputs; tariff accuracy does not establish mining profitability.

**Proposed safe behaviour:** never silently replace missing state with zero, a missing price with yesterday’s price, or a complex plan with a national average. If essential information expires, disable automatic cost-based decisions or use an explicitly selected, visibly conservative fallback. Respect electrical/supply limits independently of any negative-price incentive.

## 7. Deterministic edge-case tests to implement

**Synthetic fixtures only.** `U` means an arbitrary test currency unit; none of these prices represents a utility or a private configuration. Tests specify exact expected mathematical behaviour; jurisdiction sources motivate the rule families but do not supply these fixture prices.

| ID | Fixture | Required result |
| --- | --- | --- |
| T01 — energy units | 2 kW for 30 minutes at 3 U/kWh | 1 kWh; 3 U. Reject confusing W, kW, kWh or currency subunits. |
| T02 — fixed fee cancellation | Same plan, same two chargeable days, 4 U/day; add 1 kWh at 2 U/kWh | Total bills retain 8 U fixed; marginal cost is 2 U, not 10 U. |
| T03 — progressive boundary | First 100 kWh at 1 U; excess at 2 U; baseline 99 kWh, mining 2 kWh | `B(99)=99`, `B(101)=102`, difference 3 U; earlier units are not repriced. |
| T04 — all-units boundary | All energy at 1 U if `q<=100`, otherwise all at 2 U; baseline 100, mining 1 kWh | `B(100)=100`, `B(101)=202`, difference 102 U. Exact threshold inclusivity matters. |
| T05 — shared tier registers | 100-kWh progressive threshold shared by peak and off-peak; each records 60 kWh | Aggregate threshold usage is 120, not two fresh 60-kWh allowances. A separate-per-register fixture must produce its explicitly different result. |
| T06 — seasonal allowance | A cycle has 10 days at 2 kWh/day allowance and 20 days at 3 kWh/day | Cycle allowance is 80 kWh; no full-month allowance for either season and no reset at the season boundary. |
| T07 — different reset clocks | Energy billing cycle resets on day 15; prepaid-purchase block resets on day 1 | Day 1 resets only purchase state; day 15 resets only energy state. Annual and demand history remain intact unless separately due. |
| T08 — annual reconciliation | Annual first 100 kWh at 1 U, excess 2 U; two provisional monthly bills charge all 60 kWh each at 1 U | Annual charge is 140 U; true-up adds 20 U, not another complete annual bill. Next annual period starts with fresh annual energy state. |
| T09 — explicit two-month normalisation | Fixture defines a two-month bill as `2 * monthly_bill(total_kWh/2)`; total 202 kWh, monthly threshold 100 | Select monthly-average 101, not 202. With T04’s all-units function, total is 404 U. Reject applying a one-month threshold to the two-month total. |
| T10 — signed retail rate | 1 kWh at -2 U/kWh and unchanged fixed fees | Marginal energy cost -2 U; no zero clamp. With a separate +3 U/kWh component, total marginal cost is +1 U. |
| T11 — dynamic gaps/overlaps | Requested interval has a missing price; another interval has two unversioned conflicting prices | Unknown/conflict error, not zero, arbitrary selection, or silent carry-forward. A later correction follows an explicit revision rule. |
| T12 — interval boundaries | Adjacent price records `[00:00,00:30)` and `[00:30,01:00)` | An instant at 00:30 belongs only to the second record. Sort descending API responses and follow pagination before evaluating coverage. |
| T13 — spring clock change | Synthetic zone advances UTC+00 to UTC+01 at 01:00Z; the local transition day runs from 00:00Z to 23:00Z | 46 half-hours; 1-kW constant load consumes 23 kWh. A calendar-day fee is charged once. |
| T14 — autumn clock change | Synthetic zone falls UTC+01 to UTC+00 at 01:00Z; local day runs from previous-day 23:00Z to current-day 24:00Z | 50 half-hours; 25 kWh at 1 kW. Repeated local 01:30 instants remain distinct. |
| T15 — standard versus civil time | A fixed UTC+10 tariff starts peak at 17:00; display clock is UTC+11 | Peak starts at displayed 18:00. A civil-time 17:00 plan must differ. Changing UI time zone must not alter either bill. |
| T16 — calendar precedence | Holiday overrides weekday peak; special notified event explicitly overrides holiday | Holiday-only interval gets off-peak; event interval gets event rate. An unannounced future event is not backfilled into a forecast. |
| T17 — boundary revision | 1 kW from 00:00 to 02:00; revision at 01:00 changes 1 U/kWh to 3 U/kWh | 4 U energy cost; revision neither resets accumulated tiers nor applies the new price to the whole interval. |
| T18 — demand averaging | A 15-minute aligned interval has 12 kW for 5 minutes and zero for 10 | Average demand 4 kW, not 12 kW; energy 1 kWh. A separate 5-minute-demand fixture gives a different peak. |
| T19 — demand headroom | Already established peak 10 kW; candidate whole-meter interval 8 kW or 12 kW; demand price 5 U/kW | Demand increment respectively 0 U or 10 U, not miner kW times the demand price in both cases. |
| T20 — joint non-additivity | Baseline 8 kW in candidate interval, existing peak 10; two miners each add 2 kW there | Each alone adds no demand cost; together create a 12-kW peak. Joint charge at 5 U/kW is 10 U. |
| T21 — ratchet | Billing demand is `max(current_peak, 0.8 * lookback_peak)`; lookback 20 kW; current 10 then 15 then 17 | Billable demand is 16, 16, 17 kW. Resetting current-month maxima does not clear lookback. |
| T22 — demand units | 8 kW at power factor 0.8 under a fixture specifying `kVA=kW/PF` | 10 kVA. If power factor/apparent power is absent, return unsupported, not an assumed 8 kVA. |
| T23 — tax inclusion/order | Tax-exclusive energy 10 U, excise 2 U, VAT 10% on both | 13.2 U. Equivalent inclusive input must not have VAT added again; a different taxable-base fixture must differ. |
| T24 — component multiplier | Supply component 2 U/kWh, network component 3 U/kWh; peak factor 2 applies only to supply | Delivered peak rate 7 U/kWh, not 10 U/kWh. |
| T25 — minimum-charge floor | Fixture bill is `max(10 U, energy_charge)`; energy charge rises 8 to 9, then 11 | First increment costs 0 U; change from 8 to 11 costs 1 U. Do not add the 10-U floor as a second energy charge. |
| T26 — prepaid purchases | First 100 purchased kWh at 1 U, excess 2 U; buy 60 then 60 within a calendar month | Energy acquisition cost 140 U, equal to one 120-kWh purchase before rounding. Consuming those units later does not reset purchase history; next purchase month resets its block counter, not inventory. |
| T27 — prepaid accrued fee | 1 U/day accrued since last purchase; purchases after 10 days then 5 more days | Collect 10 then 5 U; do not collect 15 U again or discard accrued fees at month-end. Cash/token balance and energy cost remain separate. |
| T28 — export opportunity | Baseline exports 2 kWh; export price 1 U/kWh; mining consumes 1 kWh, still no import | Marginal cost is 1 U lost export payment, not zero or the import price. |
| T29 — export/import crossover | Baseline exports 1 kWh; add 2 kWh load; export price 1 U, import price 3 U | Lose 1 U export and buy 1 kWh for 3 U: total increment 4 U. |
| T30 — restricted credits | 10 U eligible consumption charge, 5 U ineligible fixed charge, 20 U credit balance | Cash due 5 U; use 10 U credit and retain 10 U subject to the specified expiry rule. Do not refund 5 U or erase fixed fees. |
| T31 — expiry/true-up isolation | Fixture credit expires at an explicit instant while annual energy tiers reset at another | Apply each transition once; no credit survival by moving time zones, no accidental clearing of unrelated state. |
| T32 — rounding stage | Supplier-style half-even fixture: interval kWh 0.015 and 0.025; rate 1 U/kWh | Each rounds to 0.02 kWh before multiplication; total 0.04 U. Do not import this rule into every supplier adapter. |
| T33 — causality/purity | Evaluate a future schedule twice from the same opening state, then evaluate baseline | Identical results and unchanged live state. Prices/events published after decision time cannot influence the historical decision. |
| T34 — plan identity | Change only UI language/formatting; separately change plan revision at a validity boundary | UI changes do not change bill values; revision changes take effect only for their valid interval. Ambiguous plan/territory selection is rejected. |

Fixture families T03/T06 correspond to baseline/tier structures; T04/T09 to genuine non-telescopic selection; T13–T15 to clock distinctions; T18–T22 to demand/capacity; T26–T31 to prepaid/export state. They are **not** assertions that each named supplier uses every synthetic rule.[^us1][^in1][^gb3][^au2][^za1][^za2][^ca2]

## 8. Privacy defaults

**Proposed defaults, not a claim about current storage behaviour:**

- Offline, manually selected tariff configuration first. No location inference from language, IP address, currency, account metadata or machine settings.
- Keep entered rates, consumption/generation traces, purchases, credit/demand balances and eligibility selections local. Default telemetry, automatic bill uploads, supplier-account access and public configuration sharing to off.
- Prefer public product/price endpoints where available; Octopus documents public product endpoints separately from authenticated account/consumption endpoints. Do not fetch the latter merely to discover a public rate.[^gb3]
- Before any optional live feed, disclose destination, requested plan/territory, cadence and retained data. Public feed queries may still reveal a selected region/plan; do not attach consumption or mining data.
- Collect only the eligibility flag/allowance needed to calculate, not supporting income, medical, property or household documents. Do not retain bills or account/meter identifiers by default.
- Public exports and issue diagnostics must be opt-in and redacted, using synthetic examples. Exclude credentials, account identifiers, device/wallet identifiers, private rates, precise consumption traces and raw personal configuration. OEB explicitly notes that bills contain private account and usage information.[^ca2]
- Provide deletion/export controls; redact logs and use operating-system credential storage for an explicitly enabled authenticated connector. Never commit secrets or private configuration into a public tariff pack.

## 9. Source-change and revalidation policy

**Proposed release/operations policy:**

1. **Provenance per component:** retain issuing authority, exact source URL, document/schedule number, page/section, original language, retrieval timestamp, effective interval, supersession status, tax-inclusion flags and review status. Record a content hash when a source snapshot is actually captured; this note does not invent hashes. Preserve original source text/attribution alongside a reviewed translation if redistribution is permitted.
2. **Separate “retrieved”, “announced” and “effective”.** On the retrieval date, OEB’s page already displayed November 2026 prices and TEPCO displayed an upcoming November revision. “Most recently fetched” is not “effective now”. PG&E’s consolidated schedule also contains sheets with differing effective dates.[^ca1][^jp1][^us1]
3. **Follow supersession explicitly:** Zhejiang’s 2026 notice expressly repeals its 2024 commercial rule; Eskom’s current residential structures differ from old inclining-block explanations. Check both structure and numeric parameters, not only percentage increases.[^cn2][^za1]
4. **Check before enabling any preset** and at each announced effective date, contract renewal, tariff-code/territory change, tax change, meter replacement and customer-reported discrepancy. As a conservative maintenance policy, recheck static public presets at least monthly; more frequently where the issuing authority announces updates. This cadence is a recommendation, not a utility guarantee.
5. **Dynamic/event feeds:** validate every decision interval for matching plan, currency, time coverage, publication time and expected refresh. Refresh when the supplier publishes; do not infer a universal publication hour. Version corrections. Expired/missing coverage disables verified dynamic scheduling. Monthly flags/adjustments need month-specific refresh, not a live-wholesale label.[^gb2][^br2][^jp2]
6. **Review changes before activation:** retain immutable prior plan revisions, generate a semantic diff, rerun boundary/rounding fixtures, and require a reviewed adapter update for changes in structure, eligibility, tax base or settlement. Do not automatically execute downloaded documents, macros, scripts or supplier workbooks.
7. **Validate against official worked examples/calculators** and an optional locally redacted bill. Document rounding tolerance and excluded components; investigate residual differences instead of adding an unexplained correction factor. KSEB’s calculator itself warns that consumer status affects actual bills.[^in4]
8. **Confidence expires visibly:** distinguish source-verified structure, customer-supplied parameters, estimated baseline and unsupported components in the UI and export. If a source cannot be retrieved or its applicable revision cannot be established, preserve the last evidence as historical and remove “verified current” status. Do not silently switch providers, territories or plans.

## 10. Evidence limits and attribution

- This is representative research, not an exhaustive survey of ten national electricity systems. Commercial classes, taxes, prepaid debt recovery, reactive power, wholesale settlement, critical-peak events and generation transitions can require additional adapters.
- Historical provincial notices establish historical structures, not current eligibility. Kerala full-PDF verification remains outstanding. Several otherwise verified sources are explanatory pages rather than complete contractual schedules; their unresolved settlement details are not certified here.
- No personal bill, private rate, location inference, mining observation, account configuration or identifier was used. No runtime package was installed, and no downloaded executable, script, macro or supplier workbook was executed for the research.
- This document paraphrases primary sources and attributes distinctive tariff terms to their issuers. Linked documents retain their original copyright, licence and attribution; this note does not relicense their content or modify CanaryMiner’s upstream notices. Any later redistributed source snapshot must retain applicable notices and satisfy its source’s reuse terms.

## Primary-source register

Each reference records the **retrieval/review date**, not an assertion that its rates remain in force. Except where explicitly marked index-text-only, relevant text was retrieved from the official source directly or through a document reader. Reader-generated “published” timestamps were not treated as legal effective dates.

[^us1]: Pacific Gas and Electric Company, **Electric Schedule E-1, Residential Services**, sheets 1–6, including baseline and seasonal-change conditions and bundled/unbundled billing. Retrieved 2026-10-28. https://www.pge.com/tariffs/assets/pdf/tariffbook/ELEC_SCHEDS_E-1.pdf

[^us2]: Pacific Gas and Electric Company, **Electric Schedule B-19, Medium General Demand-Metered TOU Service**, maximum-demand definition and charge/seasonal/option conditions. Retrieved 2026-10-28. https://www.pge.com/tariffs/assets/pdf/tariffbook/ELEC_SCHEDS_B-19.pdf

[^us3]: Pacific Gas and Electric Company, **Solar Bill**, legacy NEM monthly readings and annual true-up explanation. Retrieved 2026-10-28; relevant text reviewed from the indexed official page. https://www.pge.com/en/account/billing-and-assistance/understand-your-bill/solar-bill.html

[^us4]: Pacific Gas and Electric Company, **Solar Billing Plan**, residential/business monthly and annual billing, export credits and excluded business charge offsets. Retrieved 2026-10-28. https://www.pge.com/en/clean-energy/solar/getting-started-with-solar/solar-billing-plan.html

[^gb1]: Ofgem, **Energy price cap and standing charges explained**, scope, standing charges and payment/region distinctions. Retrieved 2026-10-28. https://www.ofgem.gov.uk/your-energy-supply/your-energy-bill/energy-price-cap-and-standing-charges-explained

[^gb2]: Octopus Energy, **Agile Octopus**, first-party tariff product description, half-hourly pricing, negative prices and daily standing charge. Retrieved 2026-10-28. Historical marketing passages are not used as current parameter evidence. https://octopus.energy/agile/

[^gb3]: Octopus Energy, **REST API: API Endpoints**, product, consumption and price endpoint semantics, UTC/DST examples, inclusion flags and half-even consumption rounding. Retrieved 2026-10-28. Historical product/formula examples are not treated as current contract terms. https://docs.octopus.energy/rest/guides/endpoints

[^gb4]: Ofgem, **Smart Export Guarantee (SEG)**, geographic/eligibility scope and positive metered-export payments. Retrieved 2026-10-28. https://www.ofgem.gov.uk/environmental-and-social-schemes/smart-export-guarantee-seg

[^fr1]: Commission de régulation de l’énergie, **Annex B to the 15 July 2026 TRVE deliberation**, especially Annex B1 pp. 1–4: definitions, tax basis, network-component inclusion, options, day colours, contracted capacity and retirement conditions. Retrieved 2026-10-28. https://www.cre.fr/fileadmin/Documents/Deliberations/2026/260715_2026-147_TRVE_Annexe_B.pdf

[^fr2]: Commission de régulation de l’énergie, **Understanding regulated electricity sale tariffs (TRVE)**, price components and taxation explanation. Retrieved 2026-10-28. https://www.cre.fr/consommateurs/comprendre-les-tarifs-reglementes-de-vente-delectricite-trve.html

[^au1]: Australian Energy Regulator, Energy Made Easy, **Which type of energy tariff is right for you?**, electricity tariff and meter requirements. Retrieved 2026-10-28. https://www.energymadeeasy.gov.au/article/electricity-tariffs

[^au2]: United Energy, **2026–27 Retailer Network Tariff FAQs**, May 2026, especially pp. 3–5: interval length, kW/kVA, DST, monthly reset and dedicated-circuit treatment. Retrieved 2026-10-28. These are network—not necessarily retail—billing rules. https://media.unitedenergy.com.au/factsheets/2026-27_Retailer-FAQ-United-Energy.pdf

[^cn1]: Zhejiang Provincial Development and Reform Commission, **Notice forwarding third-regulatory-period provincial grid transmission/distribution pricing**, document 2023 No. 139, 19 May 2023, Annex 1 residential/agricultural sales table. Retrieved 2026-10-28 from an official provincial government document host. Historical structural evidence; later amendments require checking. https://zjjcmspublic.oss-cn-hangzhou-zwynet-d01-a.internet.cloud.zj.gov.cn/jcms_files/jcms1/web3586/site/attach/0/f8ae94630d434f5eb846e483fedc4df6.pdf

[^cn2]: Zhejiang Provincial Development and Reform Commission and Provincial Energy Bureau, **Notice optimising commercial and industrial time-of-use pricing**, dated 28 May 2026, effective 1 July 2026, including the repeal of the 2024 notice. Retrieved 2026-10-28. https://fzggw.zj.gov.cn/col/col1229123351/art/2026/art_8282044c7b1b469c82f5fd9d2c27a41d.html

[^cn3]: Jilin provincial official disclosure site, **Notice on trial residential inclining-block electricity pricing**, document 2012 No. 157, dated 27 June 2012, republished 7 February 2021. Retrieved 2026-10-28. Historical evidence of annual reconciliation and partial-year treatment, not a verified-current preset. https://xxgk.jl.gov.cn/zcbm/fgw/xxgkmlqy/202102/t20210207_7938025.html

[^in1]: Kerala State Electricity Board Limited, **Annexure I: Comparison of revised tariff for retail supply**, including revisions for 1 April 2025–31 March 2027, domestic monthly telescopic/non-telescopic labels and phase/fixed-charge distinctions. Reviewed 2026-10-28 from indexed text of this official PDF; direct full-PDF retrieval failed. https://kseb.in/uploads/Downloadtemsuppy/Annexure-I%20Comparison%20of%20rates-174021070633101455.pdf

[^in2]: Kerala State Electricity Board Limited, official copy of **Gazette tariff schedule**, including Annex E domestic ToD provisions. Reviewed 2026-10-28 from indexed text; direct full-PDF retrieval failed. Detailed current application remains unverified. https://kseb.in/uploads/Subsubmenu/Latest%20Gazette%20Notifications1712202411:10:11.pdf

[^in3]: Kerala State Electricity Board Limited, **Tariff Revision Circular 2025–2027**, dated 22 March 2025, monthly/bi-monthly and interim revision billing instructions. Reviewed 2026-10-28 from indexed text of the official PDF; full-PDF retrieval failed. https://kseb.in/uploads/Downloadtemsuppy/Tariff%20Revision%20Circular%202025-2027-17429697391004759589.pdf

[^in4]: Kerala State Electricity Board Limited, **Electricity Bill Calculator**, generic/advanced billing-cycle fields and consumer-status disclaimer. Reviewed 2026-10-28 from the indexed official interface; no account details submitted. https://bills.kseb.in/

[^jp1]: TEPCO Energy Partner, **Price List—Lighting**, monthly tax-inclusive Meter-Rate Lighting B/C and prospective revision tables. Retrieved 2026-10-28. Original Japanese is authoritative; the site’s English rendering is expressly reference-only machine translation. https://www.tepco.co.jp/ep/private/plan/chargelist01.html

[^jp2]: TEPCO Energy Partner, **Fuel Cost Adjustments, etc. included in Electricity Rates for November 2026**, 29 September 2026, pp. 1–3, including contract/area scope, application month, subsidy and proration footnotes. Retrieved 2026-10-28. https://www.tepco.co.jp/en/ep/about/newsroom/press/archives/2026/pdf/260929e0101.pdf

[^br1]: Agência Nacional de Energia Elétrica, **Tarifa Branca**, eligibility, weekday periods, weekends/national holidays and distributor-specific schedules. Retrieved 2026-10-28. https://www.gov.br/aneel/pt-br/assuntos/tarifas/tarifa-branca/introducao

[^br2]: Agência Nacional de Energia Elétrica, **About tariff flags**, monthly applicability, per-energy surcharge structure and isolated-system exclusion. Retrieved 2026-10-28. https://www.gov.br/aneel/pt-br/assuntos/tarifas/bandeiras-tarifarias/sobre-bandeiras-tarifarias

[^br3]: Agência Nacional de Energia Elétrica, **Cost of energy delivered to consumers**, tariff components and federal/state/municipal taxes. Retrieved 2026-10-28. https://www.gov.br/aneel/pt-br/assuntos/tarifas/entenda-a-tarifa/custo-da-energia-que-chega-aos-consumidores

[^br4]: Agência Nacional de Energia Elétrica, **Distributed generation**, SCEE overview, credit validity, Group B availability cost and Group A contracted demand. Retrieved 2026-10-28. This overview is not a full transition-regime settlement specification. https://www.gov.br/aneel/pt-br/assuntos/geracao-distribuida

[^za1]: Eskom, **Tariffs and Charges Booklet 2026–2027**, especially definitions pp. 8–11 and residential sections pp. 27–29, with prepaid component combination, Homeflex Gen-offset and demand/capacity distinctions. Retrieved 2026-10-28. https://www.eskom.co.za/distribution/wp-content/uploads/2026/07/Eskom-Tariff-booklet_Final.pdf

[^za2]: City of Cape Town, **Understanding Residential Electricity Tariffs in Cape Town—2026/27**, applicable from 1 July 2026, especially pp. 1–6; explicitly excludes Eskom-supplied areas. Retrieved 2026-10-28. https://capetown.gov.za/media/r5cn3hlu/understanding-residential-electricity-tariffs.pdf

[^ca1]: Ontario Energy Board, **Electricity rates**, RPP alternatives, seasonal thresholds, schedules, delivery distinction and Global Adjustment treatment. Retrieved 2026-10-28; the page already displayed prices effective 1 November 2026, which are not treated here as current October prices. https://www.oeb.ca/consumer-information-and-protection/electricity-rates

[^ca2]: Ontario Energy Board, **Net metering**, permitted credit offsets, up-to-12-month carryover, agreement requirements and bill privacy. Retrieved 2026-10-28. Exact implementation must also follow the regulation and utility agreement linked there. https://www.oeb.ca/consumer-information-and-protection/net-metering
