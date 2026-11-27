# Native combined tariff and declared opening (P-05c)

**S → Electricity → Combined tariff / declared whole-meter opening** edits and saves the supported [pure electricity model](electricity-core.md). The idle `settings` command exposes the same page. It is an **offline declaration**, not an enabled electricity collector, measured bill/reset ledger, supplier-verified adapter or optimizer. The [session GPU-board flat-energy model](session-energy.md) keeps its own controls/save target.

Requirements R-UI-007/008, R-TAR-001–006 and R-PRIV-002 remain partial. Settings saves a **user-declared opening scenario** for the composable tariff. Measured state transitions and separate allowance resets remain unfinished.

## Native editing and review

- Arrows/Tab select fields. Enter opens a section/table or edits a value; Space toggles acknowledgement. Esc cancels an edit, returns to the parent, then offers review/discard/continue on dirty exit. Q/D/L/S inside editors are text; Ctrl+C stops. No browser/GUI runtime is added.
- A appends a **blank** revision/calendar/dynamic/band row; X confirms removal from the draft. Changing a rule mode never clears an incompatible table automatically; validation fails until explicitly edited.
- Empty monetary/quantity text is missing, not zero. Cycle/day fees, multiplier and opening amounts require explicit entries, even when zero/one. Canonical signed decimals allow at most 38 coefficient digits / 18 fractional places. The input format uses no exponents or binary floating point. Clearing an optional minimum/final band/date chooses `null`, distinct from zero.
- Select UTC, a whole-minute fixed standard offset within ±18 hours, or an [explicit IANA zone](#use-named-tariff-and-reset-clocks) with the reviewed offline data. Rule/opening instants use **UTC milliseconds**; local confirmation uses seconds. Revision ends are exclusive; all-units upper thresholds are inclusive.
- Field names match the schema. Hints explain enums, minute bounds, masks and units. Weekday bit 0 is Monday; month bit 0 is January. Dates use YYYY-MM-DD or null. Split overnight windows; explicit priorities resolve declared calendar events.
- V/F2 shows before/after changes, the complete draft and exclusions. Enter/F2 saves only `tariff-declaration.json`. Errors preserve drafts; field-specific errors focus the relevant field. Cross-field failures identify the plan/opening contract. UI/runtime/manual-price/flat-energy drafts save independently.
- Confirmation stamps the local action only; it cannot alter publication/effective bounds, renew a feed or authenticate a supplier. Claimed publication after confirmation is rejected. Loading never silently renews a declaration.

The supported terminal remains 60+ columns and 12+ rows. Focus precedes a long nested path; review scrolls. NO_COLOR retains textual focus. ASCII input remains explicit until Unicode/RTL work. This does not certify screen-reader access or sub-60 layouts.

## Represented contract

The strict [declaration schema](../config/tariff-declaration.schema.json) and [invented v1 example](../config/tariff-declaration.example.json) contain:

1. Independent jurisdiction, territory, supplier, product, currency, plan version key, clock and unverified local source reference. Three-letter currency is an identifier, not FX/legal-tender qualification.
2. Contiguous revisions with constant/calendar/dynamic time pricing, signed rates, publication instants and additive shared-register progressive or true all-units bands. Calendar fallback/events, fees, optional minimum and one whole-subtotal multiplier retain the pure core's exact documented order.
3. Separate opening key, whole-meter scope/adapter declaration, cycle begin/end/through, accumulated import kWh and **pre-multiplier accrued subtotal**. Origin is always **declared forecast**; typing a meter label cannot create observed telemetry. At cycle start both amounts must explicitly be zero; mid-cycle requires caller-supplied amounts and the clock version.
4. Version 1 uses `explicit_cycle_only`. Version 2 defines a monthly whole-meter cycle. Version 3 adds explicit daily-fee allocation to the monthly cycle. Version 4 supports named clocks. Version 5 supports an annual cycle. Version 6 defines a two-month cycle using monthly-average all-units pricing. Version 7 adds seasonal progressive allowances. Version 8 adds [component-specific unit-rate multipliers](#apply-a-multiplier-to-part-of-the-unit-rate). Version 9 adds [maximum average demand](#charge-for-maximum-average-demand). Version 10 adds a [historical demand floor](#use-a-historical-demand-floor). Version 11 adds [apparent maximum demand](#charge-for-apparent-demand-in-kva). Version 12 declares a [separate payment for exports](#price-electricity-exports-separately). Version 13 adds [restricted opening credits](#apply-restricted-credit-at-cycle-close). Version 14 adds [signed scheduled export rates](#price-electricity-exports-separately). The rounding default is `final_display_only`. All versions share the basis, origin, time-version and acknowledgement rules. Rate changes do not reset shared quantity.

The decoder calls `validateTariffOpening`, not a fake zero-power bill. This proves **structure/binding**, not sufficient price/trace coverage or a forecast. Evaluation still requires complete bound trajectories, prices published at the decision instant, unambiguous calendar winners and a supported horizon. Dynamic gaps remain missing; overlapping records reject. Minimum/all-units need the full remaining cycle. Saving does not assert that a future decision can be priced.

A saved opening is an offline scenario assertion, **not** an observed accumulator, recovered bill, source cursor, meter calibration or permission to reset an actual ledger. Qualified whole-meter/baseline telemetry and an isolated durable state owner remain runtime prerequisites. Editing the declaration does not rewrite measured history.

Version 15 adds [ordered supply taxes](#apply-ordered-supply-taxes). Unsupported families include contracted capacity, reactive power, export netting and supplier-specific restricted export credits, live prepaid enrollment and debt, independent supplier tariff registers and automatic true-up, other proration, threshold revisions and other provider rounding rules. Supplier-specific tax rules require their own qualification.

## Apply ordered supply taxes

In the combined tariff editor, select Plan and press **T** to add tax periods to
the draft. Set `subtotal_multiplier` to `"1"` to use ordered taxes. Use **A** to add
periods, rules, components or prior tax keys. Enter on a rule's Kind cycles
through fraction, measured import kWh and billed import kWh. Review and save the
complete draft with the existing controls.

With version 15, one period must cover each complete supply cycle. Periods use non-overlapping
UTC ranges in time order, plus an explicit publication instant. The cutoff
must be at or after the selected period's publication. Use separate periods
when rules change at supply reset. Use [service-time allocation](#apply-a-tax-change-inside-a-supply-cycle)
for a change within a supply cycle.

For example, this fragment under `plan` adds an excise of 1 per measured
import kWh. The VAT rate is 0.1 on the energy charge plus that excise.

```json
{
  "tax_periods": [{
    "begin_utc_ms": 1704067200000,
    "end_utc_ms": 1706745600000,
    "published_utc_ms": 1704067200000,
    "rules": [
      {"key": "excise", "kind": "imported_kwh", "rate": "1", "included": false},
      {"key": "vat", "kind": "fraction", "rate": "0.1",
       "components": ["energy"], "included_in": [], "prior_taxes": ["excise"]}
    ]
  }]
}
```

At 2 kWh and a pre-tax energy charge of 10, the excise is 2 and the VAT is
1.20. The closing supply estimate is 13.20. Selecting only energy for VAT
would produce 13.00.

- `imported_kwh` uses exact measured energy. `billed_kwh` uses the half-hour
  rounded quantity when that tariff rule applies and measured energy otherwise.
  Set `included` to true when the unit charge is already included in energy.
- A fraction selects `energy`, `daily_fee`, `cycle_fee`, `demand` or
  `minimum_adjustment`. The closing minimum is applied before ordered taxes.
  Each prior tax key must name a distinct earlier rule with a separate charge.
  A signed energy credit produces a signed result on a selected fractional base.
- If a fractional tax is already in the price, put those components in
  `included_in` and leave them out of `components`. A fully included tax has
  empty `components` and `prior_taxes` arrays. To review a rate already inclusive
  of both example taxes, enter the inclusive energy price, mark the excise
  included and declare VAT included in energy. A 13.20 inclusive charge then
  stays 13.20.

Version 15 requires `opening.tax_bases` with `energy`, `daily_fee`,
`cycle_fee`, `demand` and `billed_kwh`. Use zeros at cycle start. A mid-cycle
opening uses the independently reviewed amounts before added taxes. The first
three bases sum to the declared opening `accrued_subtotal`. The daily-fee
base must agree with the declared fees and allocation rule from cycle start
through the elapsed opening. The demand base
matches the charge on the established billable peak, including any declared
historical floor. Use zero for `billed_kwh` unless the half-hour rounding rule
is selected. For that rule, enter the exact cumulative billed kWh to two
decimal places. The opening must be compatible with the measured quantity
and elapsed half-hours. When billed kWh is zero, the energy charge is zero.
Ordinary and extended meter archives preserve the declared
opening and verify the exact tax bases and charges on replay.

`meter-bill` displays each tax base, amount added and any included components.
The closing estimate includes ordered taxes. Supply invoice comparisons use
that estimate. Separate export payments keep their own pricing and are
subtracted afterward. Ordered taxes and restricted closing credits cannot
share a declaration. Rate imports with VAT-inclusive prices refuse added
tax rules. The declarations use local source evidence and display-only
rounding for tax amounts.

### Apply a tax change inside a supply cycle

In Settings, add ordered taxes, select Tax periods in Plan and press **J**.
Version 16 records this choice as `plan.tax_allocation: "service_and_cycle_close"`.
Declare adjacent, non-overlapping UTC ranges covering the whole cycle. Each
period carries its own ordered rules. The review cutoff must be at or after
publication of each period used by the reviewed prefix. A prefix ending
exactly at a change uses only the prior period.

- Energy charges and measured or billed kWh belong to their service period.
  Supply resets and tier thresholds keep their own clocks. The all-units
  tier reprices each period's energy at the new whole-cycle tier rate.
  Nonzero cumulative-meter deltas need a reading at every crossed tax
  boundary. Average-power intervals use their declared constant power on
  both sides. Rounded declarations require boundaries on UTC half hours.
- A cycle fee uses the period containing cycle start. A daily fee uses the
  period containing the first charged instant of that civil day in the cycle.
  The existing whole-day or prorated amount stays with that first instant.
- Demand and minimum-charge adjustments use the last period before the
  cycle ends. Provisional reports include accrued demand in the supply
  subtotal. They add its tax only when the cycle closes.
- Prior tax keys refer to rules earlier in the same period. Reuse the same
  key in different periods to show each period's result under the same name.

At cycle start use an empty `opening.tax_segments` array. For a later
opening, use **A** in Tax segments to add one row for each elapsed period
in time order, including a partially elapsed period. Set `begin_utc_ms`
to the first instant of that period inside this cycle. Enter the reviewed
`imported_kwh`, `energy`, `daily_fee`, `cycle_fee` and `billed_kwh` for
the part already elapsed. Enter zero billed kWh for ordinary tariffs.
The program checks the segment totals, deterministic charges and feasible
rounded quantities against the opening. Where historical rates cover a
segment, it bounds the charge using those rates and checks the exact amount
when the lowest and highest possible charge match. Where the charge depends
on the timing of individual readings, use the reviewed historical
breakdown. Keep the demand peak in its original opening field and leave
its charge out of `opening.accrued_subtotal`. The model derives the amount.

The report groups tax lines by period start. Saved reviews preserve the
allocation and exact segment amounts. Confirm the supplier's attribution
rules before applying this method to a specific bill.

## Use named tariff and reset clocks

The combined tariff editor supports independently selected IANA rate and recurring reset clocks
in an opt-in installation with the verified 2026e database. Select the Clock object under Plan
or the recurring reset policy and press **Z**. Enter the IANA zone in the Zone field and review the draft.
Changing the rate clock in a cycle requires re-entering the accrued opening subtotal.
Changing the reset clock requires deriving the cycle again with **G** and reviewing its opening.
Press **Z** again to select UTC. The rate clock controls calendar prices and daily fees.
The reset clock controls monthly, annual or two-month boundaries.

With a named clock, an explicit or monthly cycle uses version 4. An annual cycle uses version 5
and a two-month cycle uses version 6. Version 7 also accepts this clock object.

```json
{"kind":"named_iana","zone":"America/New_York","time_database_version":"2026e"}
```

The opening time database version matches the rate clock. Use **2026e** for a named rate
clock and **explicit-fixed-clock** for UTC or a fixed-offset rate clock. The reset clock
keeps its own version. Recurring reset dates use the first valid instant of the selected local date. A completely
skipped date blocks derivation. The older UTC and fixed-offset declaration schemas remain readable.

Calendar prices follow the chosen zone across transitions. A repeated wall-clock hour prices
both elapsed hours. A skipped wall-clock hour contributes zero elapsed usage. With daily-fee
proration, split by the actual minutes in the tariff day, including 23-hour and 25-hour days.
A split that cannot be stored exactly within the supported decimal precision is refused.
Historical second-level boundaries require a supported whole-minute allocation to prorate.
An out-of-order repeated tariff date caused by a historical rollback across midnight is refused.

Named configurations, including archived meter reviews, require the reviewed version on replay.
A missing, changed, or mismatched dataset withholds named-time results. No operating-system
default timezone is selected. See [local build and installation](electricity-core.md#local-installation-of-the-reviewed-tariff-time-data).

## Derive a monthly whole-meter cycle

Select `reset_policy` and press **M** to choose the monthly policy. Open the policy with **Enter** and enter a day
from 1 to 31. The draft starts with the rate clock as a suggested value. Review the reset clock and edit it if your
billing cycle uses a different clock. The saved reset clock is independent of the rate clock.
Each month starts the cycle at the first valid local instant on the chosen day, usually 00:00. Short months use their last day and the original day applies
again the following month.

Enter the opening's through-time in UTC milliseconds, then press **G** to derive the dates of the cycle containing
that instant. At an exact boundary, the new cycle starts. If the start changes, clear both opening amounts with
**Backspace** in their editors, derive the dates, then explicitly enter the amounts for the new period. At the start
of a cycle, both amounts are zero. The tariff revisions must cover the full derived cycle. Use **V** to review the
policy, dates and values, then **Enter** to save. Changing the reset policy requires new plan and opening keys.

Existing declarations retain their explicit dates. Press **M** on the monthly policy to return to manually entered
dates, then review any edits before saving. The monthly rule derives dates in the draft and validates the saved
opening against them. It does not advance with elapsed time or reset a bill, readings or accrued amounts.

### Set an annual whole-meter cycle

Select `reset_policy` and press **Y**. Enter a month from 1 to 12, a day from 1 to 31 and a reset clock. Use **G** to derive the dates, then enter both opening amounts for that annual period. Review the plan and opening keys before saving. The editor clears those amounts when the schedule changes to or from annual.

Version 5 stores this annual policy. For a February 29 reset, the boundary is February 28 in a non-leap year. The reset clock can be UTC, fixed standard time or a reviewed IANA clock. Each period starts at the first valid instant of the chosen local date. A skipped local date makes that cycle unavailable.

```json
{
  "annual_month": 2,
  "annual_day": 29,
  "clock": {"kind": "utc", "fixed_offset_seconds": 0}
}
```

Usage accumulates from the annual opening across months and rate revisions. The shared tier register resets at the next annual boundary. The cycle charge and any minimum apply once per annual cycle. For month-by-month review, supply readings through the desired cutoff to obtain a provisional annual estimate. When an interval crosses the year boundary, a cumulative register file needs a reading exactly there. A version 5 review requires a version 5-capable reader. This policy defines one annual billing and tier cycle. Use the [annual invoice comparison](#reconcile-interim-supply-charges-at-annual-close) to reconcile locally declared interim charges. Automatic invoicing and supplier-specific settlement need their own rules.

### Price a two-month cycle from monthly bands

Select `reset_policy` and press **B**. Enter `1` for resets in January, March, May, July, September
and November, or `2` for the even months. Enter a day from 1 to 31 and review the reset clock.
Each cycle starts at the first valid local instant of its reset date. Short months use their
last day. Use **G** to derive the dates, enter the opening amounts, then review and save with
**V** and **Enter**. Changing to or from the two-month rule clears the draft opening amounts.

Version 6 stores this reset policy.

```json
{
  "bimonthly_start_month": 1,
  "bimonthly_day": 1,
  "clock": {"kind": "utc", "fixed_offset_seconds": 0}
}
```

Enter the all-units band thresholds, cycle charge and optional minimum as **monthly** values.
The rule charges twice the monthly bill at half the cycle's total consumption. For example, with
a monthly threshold of 100 kWh and rates of 1 and 2 per kWh, a cycle with 200 kWh costs 200.
A cycle with 202 kWh averages 101 kWh per month and costs 404. The rule selects the band
before display rounding and applies the existing subtotal multiplier once.

The plan requires one constant price revision, zero daily fees and `final_display_only` rounding.
Use readings at each two-month boundary for cumulative register files. `meter-bill` labels the rate
basis and shows the monthly-average usage at closing. Earlier reports are provisional because
later usage can change the band for the whole cycle. A mid-cycle declared opening uses the import
to date for the two-month cycle and its pre-multiplier subtotal under this same rule. Archives keep
the original monthly declaration and replay the calculation. Reopening an archive with this rule
requires a reader that supports version 6 declarations.

This offline rule covers the declared two-month formula. Supplier-specific rounding, proration
and settlement require their own rules.

### Apply a multiplier to part of the unit rate

In a revision, select `base_rate_per_kwh` and press **C**. Enter the sum of the charges that use the
factor under `component_multiplier.rate_per_kwh`. Keep the sum excluded from the factor in
`base_rate_per_kwh` or the applicable calendar or dynamic row's `rate_per_kwh`.

Set `component_multiplier.base_multiplier` for constant rates and the calendar fallback. Enter a
`multiplier` in each calendar or dynamic row. For dynamic rates, set both fallback fields to zero.
Factor zero excludes the multiplied part for that slot. The two price amounts can be signed.
Use the revision and row publication times to record when the rates and factors were known.

For supply at 2 per kWh and network charges at 3 per kWh, use this revision fragment.

```json
{
  "base_rate_per_kwh": "3",
  "component_multiplier": {"rate_per_kwh": "2", "base_multiplier": "1"}
}
```

Use `rate_per_kwh` of `"3"` and `multiplier` of `"2"` in the peak calendar row. The off-peak
unit rate is 5 and the peak rate is 7. Tier rates, fixed fees and any declared subtotal
multiplier keep their usual treatment.

Version 8 requires at least one revision with component multipliers. Other revisions
can still declare ordinary unit rates. Use new plan and opening keys after changing a
saved rule. For a mid-cycle opening, enter a subtotal priced with the revised rates.
Changing the meter binding or opening time also requires re-entering the imported quantity.
Review and save with **V** and **Enter**. Archives retain the declared amounts and factors.

This rule covers the T24 fixture. Supplier rates need separate qualification. The Octopus
published-rate import uses its own price inputs and requires a declaration without this rule.

### Charge for maximum average demand

Select `reset_policy` and press **D**. Declare the window length, its UTC offset in minutes,
the publication time and the price per kW. Supported windows divide an hour and run for
5 to 60 minutes. Offset zero aligns the windows to the UTC epoch. A 15-minute window
with offset 5 starts at :05, :20, :35 and :50 UTC. All windows in the declared cycle are eligible.

Version 9 adds the rule and `opening.peak_demand_kw`. Declare zero peak at the start of a cycle.
For a mid-cycle opening, enter the highest average kW already reached and the accrued
energy and fixed charges **before** the demand charge and subtotal multiplier. The program
adds the opening demand charge. Each later window adds a charge only for the increase
in peak. The charge is part of the subtotal before the optional closing minimum and
subtotal multiplier. Use new plan and opening keys after changing a saved rule.

The opening, review end and cycle boundaries must align with the demand grid. Use a meter
reading at each demand boundary where import is nonzero. An exact zero delta can span
multiple windows. Power-interval input assumes a uniform load within each supplied interval.
Its result is a scenario estimate when the intervals represent averages over longer periods.
The local archive retains the original readings and exact peak and charge for replay.

Five minutes at 12 kW and ten minutes at zero yield 1 kWh and a 15-minute peak of 4 kW.
The same data under a five-minute rule yields a peak of 12 kW. A plan with an established
10 kW peak and a price of 5 per kW adds zero for a later 8 kW window and 10 for a 12 kW
window. Two loads considered together can increase the bill even when each alone stays
within the existing peak.

The peak resets at the declared cycle boundary. This covers fixtures T18–T20. Separate
demand-reset calendars, eligible time bands and supplier qualification remain open.
The two-month monthly-average rule has no demand allocation.

### Use a historical demand floor

Set a monthly reset and add a demand charge. In Settings, select `demand_charge` and press **R**.
Version 10 adds `demand_charge.ratchet` with `lookback_months` from 1 to 12 and a `fraction`
greater than zero and up to one. For 80 percent, enter `0.8`.

In `opening.demand_history`, add one row for every immediately preceding completed
month, oldest first. Each row contains its exact `cycle_end_utc_ms` under the saved reset
clock and its `peak_kw` measured on the declared demand grid. Enter actual peaks before
applying any floor. The historical periods must be complete by local confirmation
and the review cutoff. Missing months or mismatched boundaries block the review. The separate
`peak_demand_kw` field still holds the current cycle's actual peak, or zero at cycle start.

The billable amount is the larger of the current month's actual peak and the lookback
maximum multiplied by the fraction. With a historical peak of 20 kW and a factor of
0.8, current peaks of 10, 15 and 17 kW produce billable demand of 16, 16 and 17 kW.
The review displays the actual peak, the floor and the billable kW. The opening subtotal
still excludes demand. A mid-cycle review calculates its change from the already established
billable demand.

At each monthly reset the actual peak of the completed cycle enters the lookback, the
oldest peak expires and the new current-month peak starts at zero. A whole-meter
comparison carries each scenario's actual history independently. Archive replay
reconstructs the same history and verifies the exact floor and billable demand for
each reviewed cycle.

This covers T21 for one all-day maximum with consecutive monthly resets. Contracted
capacity and independent reset schedules remain open.

### Charge for apparent demand in kVA

With `demand_charge` selected in Settings, press **K** to switch the draft to kVA. Enter the
price per kVA and the established kVA peak again. For a monthly historical floor, enter the
previous actual peaks in kVA. Version 11 uses `rate_per_kva`, `opening.peak_demand_kva`
and `peak_kva` in each historical row. The selected demand unit also applies to the floor
and the billed peak. Use new plan and opening keys when changing a saved demand unit.

Save the standard demand window, UTC alignment and publication time. Leave
`power_factor_va_places` empty in Settings (`null` in JSON) for measured VA only. To accept
power factors, enter the number of VA decimal places, from zero to 18. The model divides
active watts by the factor and rounds to those VA places, with ties to even, before integrating
the interval. A factor must be positive and no larger than one. Choose sufficient precision
to preserve the active watts as a lower bound. An interval with 8,000 W and a factor of
0.8 gives 10,000 VA, or 10 kVA. Energy billing continues to use the original 8,000 W.
[Electrical units and power factor](https://www.eaton.com/content/dam/eaton/products/design-guides---consultant-audience/eaton-basics-of-power-system-design-dg081001en.pdf)

In a local `meter-bill` input, use one of these document versions with the saved meter scope.

- Version 4 has the same `intervals` rows as the active-watt format. Add exactly one of
  `average_va` or `power_factor` as a canonical decimal string to each row.
- Version 5 adds the cumulative `apparent_kvah` decimal string to each `register_readings`
  row. Both registers use the same timestamps. Apparent-energy increments must be at least
  the corresponding active-energy increments. A nonzero apparent increment needs a reading
  at every demand-window boundary it crosses, even when active import is zero.

The program refuses kVA calculations without complete apparent-power evidence. It averages
apparent power by elapsed time in each aligned window and bills the highest qualifying average.
The archive records the original evidence and the exact kVA results. Review extension binds
both starting registers and all covered observations, including the source of each VA estimate.

In the whole-meter comparison, supply watts and VA for each scenario as `8000/10000`. If
the declaration permits conversion, `8000@0.8` is equivalent under that precision. The
second line describes the complete meter with mining. Enter its apparent-power evidence
independently. This covers T22. Tariffs that use kVA at the maximum kW window, reactive
charges or contracted capacity require their own rule and supplier qualification.

### Price electricity exports separately

Select Plan in Settings and press **E** to add the export rule. Version 12 declares
`plan.export_payment.rate_per_kwh` and `published_utc_ms`. Enter one nonnegative
fixed export payment per kWh in the same currency as the supply tariff. In the opening,
enter both `exported_kwh` and `accrued_export_payment` earned from the cycle start to
the opening time. Enter zero for both at cycle start. The declared rate prices new exports.
When changing the rate mid-cycle, carry forward the amount already earned and use new
plan and opening keys. A change in plan currency requires re-entering the earned
payment in the new currency. The import quantity and accrued supply subtotal retain their meanings.

To price exports by time, open the export rule and press **E** to switch it to a
schedule. Enter one to 256 rows under `plan.export_payment.dynamic` in UTC
order. Each row has `begin_utc_ms`, `end_utc_ms`, `published_utc_ms` and a
signed decimal string `rate_per_kwh`. Version 14 uses the rate from the start
instant up to the end instant. A negative rate charges for exporting. A net
export charge already incurred goes in `accrued_export_payment` as a negative
amount. The schedule must cover the reviewed period, including intervals with
zero exports. Each rate must have been published by the review cutoff and the
local confirmation time. Gaps and overlaps stop the review.

A local meter review requires explicit export evidence even when export is zero.

- Input version 6 uses `intervals` with `begin_utc_ms`, `end_utc_ms` and
  `average_net_watts`. A positive decimal string denotes import, a negative one
  denotes export, and zero denotes neither. Each row models a constant flow in one
  direction. Where import and export change direction within a metering interval,
  use the separate registers.
- Input version 7 uses `register_readings` with `utc_ms`, `imported_kwh` and
  `exported_kwh`. The last two are independent, cumulative, nonnegative decimal
  strings. Each can increase during the same interval. Take paired readings at
  billing-cycle boundaries.

With scheduled rates, the review splits constant power at each rate boundary.
For cumulative readings, supply a paired reading at any crossed rate change
when the interval contains exports. Equal rates and intervals with zero export
can be priced from a single register difference.

The report gives supply charges, export payment and net electricity cost. Active imports
alone count toward tiers, demand windows and supply floors. The separate payment does
not enter supply multipliers or supply-invoice matching. A negative net cost means the
declared payment exceeds the supply charge. The archive saves the original readings and
exact results. Extension freezes the starting readings and deltas of both registers.

In the whole-meter comparison, enter the net watts for each constant scenario. Across
one hour, moving from -2,000 W to -1,000 W loses 1 kWh of export. At 1 USD/kWh that
costs 1 USD. Moving from -1,000 W to +1,000 W loses the same payment and adds 1 kWh of
import. At 3 USD/kWh for import, the difference is 4 USD. These examples cover T28 and T29.

[Export invoice reconciliation](#compare-separate-export-payments-with-an-invoice) compares the supply and export amounts for a common cycle. [Restricted opening credits](#apply-restricted-credit-at-cycle-close) use their own declaration. Supplier-specific settlement and the combination with apparent-power demand require additional rules.
[Example of provider-specific export credits](https://www.pge.com/en/clean-energy/solar/getting-started-with-solar/solar-billing-plan.html)

### Apply restricted credit at cycle close

Select **Plan** in Settings and press **U** to add `restricted_credit` under schema version
13. Enter `accrued_import_subtotal`, the signed import-energy charge through the opening
before the subtotal multiplier, and `lots`, the unused credits at that opening. Enter
zero for the subtotal at cycle start and review it separately from `opening.accrued_subtotal`,
which still includes fees. For a mid-cycle opening, use the energy and fee breakdown
from the reviewed statement. Each lot has an `amount` in the plan currency and an absolute
`expires_utc_ms`. Use up to 16 positive lots listed in strictly increasing expiry order,
combining amounts with an identical expiry. Use an empty list for a zero credit balance.
Adding or removing the rule requires new plan and opening keys. Changing the credit
amounts or expiry dates within the same rule requires a new opening key.

Only the net import-energy charge is eligible. The calculation includes the declared
base and tier prices, rounds meter quantities only when declared, and applies the
subtotal multiplier to the eligible charge. Fixed and daily fees, demand charges, and
any minimum-subtotal top-up stay outside the eligible amount. Credit use is bounded
by the positive eligible charge and the positive gross supply charge. For example,
10 USD of eligible import, 5 USD of fixed fees and 20 USD in live credit leaves
5 USD cash due and 10 USD of credit. A negative gross supply charge remains visible
in the report, and using restricted credit never generates a cash payment.

Credits expire at their stated UTC instants. At a close that coincides with an expiry,
expiry is processed first. Closing uses the live lots in earliest-expiry order.
A monthly, two-month or annual reset clears the import quantities and accrued energy
subtotal for the next cycle. It carries any unused live lots with their original
expiries. Local clock and time-zone edits leave the expiry instants intact. When you change
the opening through-time, meter scope/adapter or currency in Settings, review the
opening credit amounts against the new declaration.

Meter review uses the usual import power or register format. It reports eligible
charges, applied, expired and remaining credit at the end of each reviewed span.
An open cycle can have expired credit and provisional eligible charges. Cash due is
reported at closing. Expiry is evaluated through the last reading, even if the selected
review cutoff is later. Saved reviews and their extensions bind the exact credit and
supply amounts. Optional invoice comparison continues to match the gross supply
charge, including for annual reconciliation.

This covers T30 and T31 for declared opening balances. The rule supports kW demand
and the declared historical demand floor. Supplier grants during a cycle, refund and
renewal policies, per-component eligibility beyond import energy, separate cash
payments for exports, and combined kVA/credit plans require additional declarations
and evidence.

### Apply a seasonal baseline allowance

Select `reset_policy` and press **A** to add the seasonal rule. Open `seasonal_allowance`, enter
the publication time in UTC milliseconds, and use **A** inside `seasons` to add each
start date and its daily allowance. List dates in calendar order. Each date starts a new daily
allowance, and the last season carries across New Year until the next start date.

Version 7 stores the seasonal rule. This example switches from 2 to 3 kWh per day on May 1.

```json
{
  "published_utc_ms": 1713657599000,
  "seasons": [
    {"start_month": 1, "start_day": 1, "kwh_per_day": "2"},
    {"start_month": 5, "start_day": 1, "kwh_per_day": "3"}
  ]
}
```

Use two progressive bands in each rate revision and clear both `through_kwh` fields. The program
sets the first band's limit to the total allowance for the complete cycle. The second band applies
to the excess. The rate clock determines which local days are in each season. A 23-hour or 25-hour
day counts once. A cycle must cover complete tariff days, and a skipped date blocks pricing.

A cycle running from April 21 to May 21 uses ten days at 2 kWh and twenty days at 3 kWh, giving
80 kWh for the whole cycle. With rates of 1 and 2 per kWh, 100 kWh costs 120 before other charges.
An open-cycle review uses the same allowance when the rule was published by the review cutoff.
The meter accumulates through season boundaries and resets only at its declared billing boundary.
A zero allowance prices all import at the excess rate.

The rule supports explicit, monthly or annual cycles and display-only rounding. Use new plan and
opening keys when changing a saved rule, then review and save with **V** and **Enter**. For a
mid-cycle opening, enter the import to date and the accrued subtotal
priced using this cycle's allowance. Changing the allowance, rate clock or cycle end clears the draft
subtotal for a mid-cycle opening. Changing the start or through-time also clears the opening import.
Meter-review archives retain the source schedule and exact allowance for each cycle.
Supplier-specific allocation and automatic settlement require their own rules.

<a id="prorate-a-daily-fee-across-the-monthly-reset"></a>

### Prorate a daily fee across a recurring reset

With a monthly or annual rule selected on `reset_policy`, press **F** to toggle the daily-fee rule. Review with **V** and save with **Enter**. For a monthly rule, enabling it uses version 3 for fixed clocks or keeps version 4 for a named clock. An annual rule stays on version 5. It stores `"daily_fee_allocation": "prorate_by_cycle_minutes"` inside `reset_policy`. Changing the rule requires new plan and opening keys. If the opening starts after the reset, the editor clears its draft accrued subtotal. Re-enter the amount using the selected fee rule before saving. Deriving a changed end for a prorated mid-cycle opening also clears the subtotal because it may include a share calculated from the previous end.

The fee for a tariff day comes from the rate revision effective at the tariff clock's midnight. Each billing cycle receives the fee times its overlapping minutes divided by the tariff day's actual minutes. For example, a 1.44 daily fee split at 18:30 in the tariff clock contributes 1.11 to the closing cycle and 0.33 to the new cycle. A revision at 18:30 can change the energy rate that minute. Its daily fee starts on the next tariff day. Each cycle books its share once, on first covered use or zero-usage coverage of that day. When the calculation first books a share, it requires a midnight revision published by the review cutoff. A declared mid-day opening may already include that share.

The allocation uses whole-minute boundaries and exact arithmetic through the cycle minimum and multiplier. Midnight-based boundaries in the same clock allocate a whole day to a single cycle. Version 2 retains its existing split-day refusal in a later cycle.

## Compare two whole-meter scenarios

Open Combined tariff from the Dashboard Settings or the idle `settings` command and press **P**.
Enter a constant whole-meter value for each scenario. Standard supply tariffs use import watts.
A [kVA declaration](#charge-for-apparent-demand-in-kva) also needs apparent-power evidence.
For a [separate export payment](#price-electricity-exports-separately), enter signed net watts.
Positive values import energy and negative values export it. Use **Tab** to change fields and
**Enter** to calculate. Each value applies from the saved opening's through-time to the selected UTC end.

For a saved monthly, annual or two-month reset rule, use **Left/Right** to select 1–12 cycles. The first selection is the remainder of
the saved cycle. Each additional cycle runs from one declared reset to the next. The same entered watts apply
through the displayed end. Later cycles start with zero imported and exported energy and zero accrued supply subtotal.
Each cycle applies its own fees, tier allowance and minimum. The comparison adds exact
amounts before display rounding. A declaration with
explicit cycle dates still has one horizon.

Every selected cycle needs complete rates known at the original comparison time. A missing rate in any selected
cycle withholds all three amounts. The first cycle uses the saved opening's daily-fee rule. Without allocation, the comparison reports the first later cycle that splits a charged tariff day. With the declared allocation, it splits the daily fee across the affected cycles. Selecting a horizon applies only to the estimate.

The comparison reports the amounts for both scenarios and their difference in the plan currency. Standard supply amounts cover only the remaining horizon.
With a separate export payment, the displayed amounts are net of the export payout.
With restricted credit, each scenario spends its own copy of the opening balance at
eligible closings. The display shows the full forecast cash due, including charges
and credits attributable to the declared opening.
The two scenarios share one opening and the same known tariff revisions. An incomplete rate schedule produces an
unavailable result. A background calculation uses the inputs and local time captured at Enter. Escape returns to the
tariff declaration and cancels the calculation. The new comparison starts with empty inputs. If a cancelled calculation
is still exiting, press Enter again when prompted. The session stop path revokes the calculation before draining workers.

The comparison is an estimate from the saved declaration and entered watts. It has no observed usage or economic-mode permission.

## Review a local meter file

Run `canary-miner meter-bill --tariff tariff-declaration.json --intervals meter-intervals.json` with an explicitly chosen
saved tariff and source file. The command reads those files and prints the review to stdout. Its estimates remain local
to the command and include any declared opening used for the first cycle.

The import-only interval file has schema version 1. For [apparent demand](#charge-for-apparent-demand-in-kva),
use input version 4 or 5. For [separate export payments](#price-electricity-exports-separately), use version 6 or 7.
In the version 1 format, use the same scope as the tariff opening and supply ordered, adjoining whole-meter
import intervals. Each interval uses UTC milliseconds and a canonical nonnegative decimal string in watts.

```json
{
  "schema_version": 1,
  "scope": "synthetic-whole-meter-scenario",
  "intervals": [
    {
      "begin_utc_ms": 1711886400000,
      "end_utc_ms": 1711887300000,
      "average_watts": "1000"
    }
  ]
}
```

The first reading starts at the saved opening's through-time. The calculation applies the recorded average watts
uniformly within each interval. This convention matters when an interval spans a tariff change or cycle reset.
The output lists reviewed UTC coverage, imported kWh, the charge change from those intervals, and the estimate for
each touched cycle. A cycle that ends before the next reset has a provisional estimate. Subsequent declared cycles start
with fresh allowances and fees from the saved rule. An interval that ends exactly on a reset closes the preceding cycle.

Add `--as-of-utc-ms MS` with a positive ASCII decimal UTC millisecond timestamp to replay a historical review.
The cutoff appears in the report. Every supplied interval must end at or before the cutoff. A rate published later cannot affect the estimate.
If such a rate is needed, the entire review is refused. Reset-day fee checks use the same cutoff. A future cutoff is
invalid. The saved declaration still supplies
the opening values, so the cutoff describes rate publication and reading coverage, and local confirmation can be later.
Omit the option to use the current UTC time.

Annual policies and policies with demand charges or separate export payment accept files up to 1.75 MiB and 18,432 intervals. Other policies accept 1 MiB and 8,192 intervals. Dense full-cycle register files can exceed these limits. A review can cover up to twelve cycles. Coverage gaps, overlap, wrong scope, future readings,
unknown rates and unsupported reset rules withhold all amounts, including earlier cycles in the same review. An
additional cycle that starts within a charged tariff day requires the declared [daily-fee rule](#prorate-a-daily-fee-across-a-recurring-reset). Use regular files in a trusted
local directory. Windows accepts native Unicode paths on local drives and rejects redirected directories. The command
prints no file paths or scope labels. Ctrl+C cancels the calculation. A stalled filesystem read can delay exit.
Estimation from submitted data and qualified live collection remain separate acceptance steps.

### Extend a saved meter review

Use `canary-miner meter-bill --extend-review OLD.cmr --intervals NEW.json --save-review NEXT.cmr`.
`NEW.json` supplies the complete prior meter coverage plus additional contiguous readings in the original format.
The command reopens the old archive at its saved cutoff, verifies the exact results, and uses its retained
tariff and supplier rate snapshot. Register continuity includes the absolute first reading. Octopus bundle
observations must advance or stay at their original time. The current review cutoff cannot precede the saved one.

Before publication the command checks historical readings and exact charges, including when the new cutoff would
make additional tariff rules eligible. The same declared reset rule applies as readings enter later cycles. An open
cycle's provisional charges can still change when more usage is measured. The original review keeps its original
cutoff and invoice. Add `--invoice FILE` when the successor has a new supply-charge declaration covering its
complete reviewed cycles.

The v4 archive retains the previous archive's SHA-256, cutoff, and last reading time with a checksum of the earlier
readings and exact amounts. Reopening recalculates the preserved meter prefix and the full new review using the
retained data. It works when the previous file is unavailable. The previous archive digest identifies the input used at
extension time. Keep that file if you want to audit its own invoice or check the archived link against the actual original.
Publication creates a new archive name and preserves all inputs. Each command covers at most twelve cycles under the
original tariff and rate window. A changed tariff or earlier reading needs a separate review.

### Review the next Octopus billing cycle

Use a complete saved Octopus Agile or Go cycle with a declared recurring reset rule.
For London monthly billing, replace the example declaration's `reset_policy` value with this object
before saving the first cycle.

```json
{"monthly_day":1,"clock":{"kind":"named_iana","zone":"Europe/London","time_database_version":"2026e"}}
```

Prepare the next declaration directly from the saved review.

```text
canary-miner meter-bill --prepare-next-cycle march.cmr --save-tariff april-tariff.json
canary-miner meter-bill --prepare-next-cycle march.cmr --save-register april-usage.json
canary-miner meter-rates --allow-public-rates --tariff april-tariff.json --payment-method DIRECT_DEBIT --save-rates april-rates.json
```

The preparation command replays the archive before writing a new local file. It uses the saved
reset rule to derive the next window and carries forward the declared meter alias, product,
region, clock and confirmation of those terms. The declaration opens with zero accrued usage and
charges. Use the payment method printed by the command and capture new prices once the cycle
ends. Review any included-price tax annotations for the new period. For a cumulative register
archive, append the new readings to the file produced by `--save-register`. It holds the exact prior
closing register as its first point. Use a new Octopus import bundle for an archive based on supplier
intervals. Each output is a separate exclusive write in the current directory.

A manually prepared declaration must start at the previous closing time and use the same meter
alias, product, region, payment method, input format and reset clock. Supply a new public rate capture and new meter
readings for the entire next cycle. Cumulative register readings must open at the absolute
closing value in the previous archive. An Octopus electricity import bundle must cover every
half hour of the new cycle exactly once. Both cycles must start with zero accrued charges and
their own applicable rates. The captured prices include VAT. Annotations for charges already
included in the captured price are accepted. Use the existing [capture command](#review-publicly-listed-octopus-agile-prices)
for the new cycle's prices. The installed capture command accepts at most 8,192 half hours of
elapsed time. Annual Go review requires a separately captured, compatible full-year snapshot.
Annual Agile prices exceed the quantity limit. The current two-month tariff model supports
all-units bands with constant rates, so it cannot roll an Octopus dynamic review.

```text
canary-miner meter-bill --next-cycle-review march.cmr --tariff april-tariff.json --intervals april-usage.json --octopus-rates april-rates.json --save-review april.cmr
canary-miner meter-bill --open-review april.cmr --previous-cycle march.cmr
```

Omit `--tariff` to derive the new declaration in memory from the prior review. You can apply
an existing capture or explicitly authorize a public rate fetch in the same command.

```text
canary-miner meter-bill --next-cycle-review march.cmr --intervals april-usage.json --octopus-rates april-rates.json --save-review april.cmr
canary-miner meter-bill --next-cycle-review march.cmr --intervals april-usage.json --allow-public-rates --save-review april.cmr
```

The installed live capture uses the preceding review's payment method and requests public
unit rates and standing charges for the derived billing window. It checks complete meter
coverage before making the requests. It uses the capture time as the review cutoff. Use the
saved-capture form to specify a historical cutoff or include tax annotations in an explicit
declaration. The command stores the new declaration, readings, public rate pages and results
together in a single private archive once the review passes. Existing files are preserved.

The command replays the prior archive at its own cutoff before calculating the new cycle. It
prices April using April's capture, so March's usage, charges and invoice keep their archived
figures. Set the new cutoff at or after the previous cutoff and the new capture and reading
observations. Add `--invoice FILE` to compare the new cycle against its supply charges.

The version 6 archive includes the previous archive's SHA-256 and the verified closing record.
You can reopen the new cycle independently to reproduce its calculation. Pass `--previous-cycle`
to verify the closing record against the original archive. Keep each archive to verify the
entire chain. Meter identity uses the declared local alias and the existing input evidence.
Supplier-account authentication and invoice settlement require separate evidence.

### Roll forward through staged Octopus billing cycles

Run the following from a private local working directory for one meter and tariff. Give the
command a verified closed review as its starting archive.

~~~text
canary-miner meter-bill --roll-forward march.cmr
canary-miner meter-bill --roll-forward march.cmr --allow-public-rates
~~~

For each cycle, replace END in the filenames with its exclusive closing timestamp in UTC
milliseconds. The reset rule in the preceding review determines the timestamp, including
changes in London's UTC offset. The review reads these files from the working directory.

| Filename | Contents |
| --- | --- |
| cycle-END.readings.json | Complete absolute register readings or supplier import intervals for that cycle |
| cycle-END.rates.json | Saved public prices for that cycle |
| cycle-END.invoice.json | Optional local invoice comparison |
| cycle-END.cmr | The resulting private review, written exclusively by the command |

The command replays existing successor archives and checks every closing record against
its predecessor. It saves each new cycle independently and stops at the first unclosed
cycle, missing required file, or failed verification. A later failure leaves already
published cycles available. Running the same command again adopts the verified prefix and
continues at the first pending cycle. Archived originals are used when replaying saved cycles.
A gap before an existing archive stops the run so the missing original can be restored.
A temporary output without a published archive is reported for inspection and preserved.

The working directory's meter-billing.lock serializes recurring reviews. The lease ends
when the process exits and the file is retained. The default mode uses saved prices only.
With explicit public authorization, it fetches prices for cycles whose saved capture is
absent. Present captures must pass validation. Without public authorization, you may pass
--as-of-utc-ms to review at a historical cutoff. A run can verify up to 120 successors,
save up to 24 new archives and request up to three public captures. Rerun to continue after
a write or capture limit. At the history limit, use the latest verified archive as the
new starting point.

The command keeps estimated charges and optional local invoice comparisons in the same
archive format as the individual review.

#### Watch for new billing inputs

Run the same command with a check interval between 1 and 86,400 seconds. The first check
runs immediately. Each subsequent check runs after the previous check and the chosen interval.

~~~text
canary-miner meter-bill --roll-forward march.cmr --watch-every-seconds 300
~~~

The process stays in the foreground and holds the workspace lease between checks. A second
recurring review in that directory receives a busy error. Keep the lock file and workspace
in place during a run. Waiting for a closing time, readings
or a saved rate capture keeps the process active. Its terminal output changes when the
review state changes. When supervised, direct output to a file or have the supervisor read it
continuously. Publish staged files by completing them and then renaming them to their final
names in the same directory. The watcher checks observation times against its refreshed clock
after reading the staged files.

Each check replays the verified archive chain. The watcher stops on invalid input, a changed
seed, a missing or changed previously verified archive, a clock discontinuity, or the history
limit. Correct the cause before restarting. Completed archives stay available for replay.
When the history limit is reached, restart from the latest verified archive.

Watch uses the current clock and cannot be combined with `--as-of-utc-ms`. The
`--confirm-electricity-import` and `--allow-public-rates` options apply to the running
process when provided. Public capture can issue up to three successful captures per check.
A capture failure terminates the watch. Use Ctrl+C or Ctrl+Break to stop a foreground run.

#### Run the watcher at login

A TLS candidate packed with `--with-billing-watch --with-named-tzdb` includes a user-level
deployment helper for its platform. Extract or install into a permanent directory first.
The watcher uses the seed's parent directory as its private workspace. Put the seed there
with the staged cycle inputs. Use one watcher for that directory, and stop it before moving
the workspace or replacing the installed release.

The following examples use saved prices. To accept local supplier import pages, add
`-ConfirmElectricityImport` on Windows or `--confirm-electricity-import` on Linux. To
request public rates for missing captures, add `-AllowPublicRates` on Windows or
`--allow-public-rates` on Linux with a public HTTPS candidate. The choices are independent.
Registering the watcher does not grant supplier account access, start mining or change
the economy setting.

On Windows, use PowerShell as the account that owns the workspace. Run from the installed
release directory. The helper prints a unique task name and refuses to replace an existing
task of that name. Keep the workspace and installed candidate on local drives. Use paths
without junctions, symbolic links or percent signs. Task Scheduler expands percent-encoded
environment variables.

If Windows has marked the helper as downloaded, verify the candidate SHA-256 and the
installed helper against the manifest before using
`Unblock-File -LiteralPath .\deploy\Register-BillingWatch.ps1`. A fresh PowerShell with
`powershell.exe -NoProfile -ExecutionPolicy RemoteSigned` selects a policy for that
process only. An organization policy can still govern whether the script runs.

~~~powershell
$task = $null
$task = .\deploy\Register-BillingWatch.ps1 -Name home -Seed 'D:\Bills\march.cmr' -Seconds 300
if ($task) {
    Start-ScheduledTask -TaskName $task
    Get-ScheduledTask -TaskName $task
    Get-ScheduledTaskInfo -TaskName $task
}
~~~

The task runs with the current user's interactive token and starts again at that user's
next logon if enabled. Task Scheduler launches the miner directly with `--watch-quiet` so
the watcher can run without a terminal. A review refusal returns code 3 in the task's last
result. Disable and stop the task, then run the same meter-bill command without
`--watch-quiet` in the workspace to inspect the failure. A Scheduler stop may terminate the
process immediately. If a temporary archive remains, inspect it before restarting.

~~~powershell
Disable-ScheduledTask -TaskName $task
Stop-ScheduledTask -TaskName $task
Unregister-ScheduledTask -TaskName $task
~~~

On Linux, use a systemd user session and run from the installed release directory. The
helper prints a unit that you can inspect before installation. Use an unused unit filename.

~~~bash
(
    set -e
    umask 077
    unit_dir="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
    unit=canaryminer-billing-home.service
    mkdir -p -- "$unit_dir"
    staged=$(mktemp -- "$unit_dir/.canaryminer-billing.XXXXXX")
    trap 'rm -- "$staged"' EXIT
    ./deploy/billing-watch.sh "$HOME/Bills/march.cmr" 300 > "$staged"
    ln -T -- "$staged" "$unit_dir/$unit"
    systemctl --user daemon-reload
    systemctl --user enable --now "$unit"
)
systemctl --user status canaryminer-billing-home.service
journalctl --user -u canaryminer-billing-home.service
~~~

The enabled unit starts with the user manager. Session and linger policy are set by the OS.
User journal history is available when the system keeps persistent user journals.
systemd sends SIGTERM and allows up to 30 seconds to stop. Use
`systemctl --user disable --now canaryminer-billing-home.service` to stop and prevent the
next automatic start.
To remove the deployment, also remove the unit file you installed and run
`systemctl --user daemon-reload`. Both platforms leave the billing workspace intact and
avoid restarting on input refusal until the next login or a manual start.

See [Windows Task Scheduler task registration](https://learn.microsoft.com/en-us/windows/win32/taskschd/taskfolder-registertaskdefinition),
[PowerShell execution policies](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_execution_policies)
and [systemd service settings](https://www.freedesktop.org/software/systemd/man/latest/systemd.service.html).

### Import Octopus consumption pages with the installed miner

Use the local JSON responses from one meter's electricity import endpoint for the complete
tariff window. The supplier also labels export readings as consumption. The
`--confirm-electricity-import` option confirms that you selected the import meter.
[Octopus consumption API](https://developer.oegb-kraken.energy/guides/rest/api-endpoints/#consumption-endpoints)

Save the responses as `page-1.json`, `page-2.json` and so on in a separate local directory. Use
a consistent page size of up to 100 rows and at most 192 pages. Use 100 rows per page for
an annual export. After the export is complete, put an `observation.json`
file in the directory with the time you obtained the complete export, recorded as a UTC
millisecond instant.

~~~json
{"observed_utc_ms":1680307200000}
~~~

For the first archive, build a normal whole-meter import bundle from a compatible saved
Octopus Agile or Go declaration. The command derives the meter alias and complete period
from the declaration.

~~~text
canary-miner meter-bill --confirm-electricity-import --import-octopus-pages march-pages --tariff march-tariff.json --save-readings march-usage.json
~~~

Supply the resulting file with `--intervals` to the usual meter-bill review. Each page must
report the expected total, form a complete set and cover every half hour exactly once.
Timestamps are stored in UTC while preserving the exact instants across changes in British Summer Time.
Pagination links are checked for a consistent meter route and omitted from the result.
A one-page export may have no link. The tariff's meter alias and the import confirmation
come from your local declaration and selection.
The import reads local files and makes no network requests.

For later periods, put the page directory in the recurring workspace under the name
`cycle-END.pages`, with the same closing timestamp used for the saved rate capture.

~~~text
canary-miner meter-bill --roll-forward march.cmr --confirm-electricity-import
~~~

When no `cycle-END.readings.json` file is present, this command reads the pages directly
and archives the verified readings as a schema 3 bundle. The preceding archive must use the
same supplier-interval input format. Select the same physical meter as the preceding
archive's local alias. A staged directory waits for its observation file before processing. The declared observation must fall between cycle close
and review cutoff. The raw pages and their pagination links remain separate from the private
review archive. Public rate acquisition still requires its own explicit option.

### Capture readings from an Octopus account

Use the account capture tool from the checkout or a v6 TLS candidate with Python 3.
Enter the UK Octopus Energy account number, import MPAN, meter serial and API key in
the masked terminal prompts. The tool uses the supplier's HTTPS account endpoint
to check that the selected import meter belongs to one property covering the full
requested window. It then reads the selected meter's half-hour consumption.
Use the same private alias and billing window as the tariff declaration.
[Octopus REST authentication](https://developer.oegb-kraken.energy/guides/rest/rest-api-basics/#authentication)
[Octopus account and consumption endpoints](https://developer.oegb-kraken.energy/guides/rest/api-endpoints/)

~~~text
python -I -B tools/capture_octopus_consumption.py --confirm-electricity-import --scope home-meter --begin 2023-03-01T00:00:00Z --end 2023-03-31T23:00:00Z --output march-usage.json
~~~

Use the output as `--intervals` for the first bill review. For a later cycle, replace
`--output march-usage.json` with `--workspace PRIVATE_DIRECTORY`. The collector
publishes `cycle-END.readings.json` atomically after verifying complete coverage.
An enabled watcher can then use the bundle together with the cycle's price capture.
The capture prompts for authorization each time. The watcher reads the local bundle
and can run after the capture process exits.

For a recurring workspace, an installed candidate can derive the alias and window
from its seed archive:

~~~text
python -I -B tools/capture_octopus_consumption.py --confirm-electricity-import --next-cycle PRIVATE_DIRECTORY/march.cmr
~~~

The collector uses the installed miner to replay the local archive chain and find
the next closed cycle missing readings. It stops before asking for credentials if
that cycle has staged readings or pages, the chain has a gap, or the seed uses a
cumulative register. The native planner can be inspected offline with
`canary-miner meter-bill --next-readings-plan SEED.cmr` from the seed's directory.
The planner reads archives while a watcher is active. It creates no files.
The collector rechecks the same predecessor and window before fetching and
again after staging the new bundle, just before publication. Use one producer
for a cycle's inputs when staging provider pages or archives in the same
workspace. The final publication preserves any readings bundle already at
the target filename.

Use a dedicated workspace for each meter. Keep the first and subsequent captures
for that meter in the same directory. When first connecting
an existing archive, verify that the selected physical meter matches the archive.
The first successful acquisition pins that selection and alias in a private
`.canary-octopus-meter.json` file. The file holds a random salt and a digest. Keep it
with the workspace when moving or backing up the data. Subsequent captures reject a
different account, meter or alias. A replacement meter uses a new workspace and
opening review. Rotating the API key does not require a new workspace.

Enter credentials only at the prompts. The tool keeps the API key and account
response in process memory for the capture and publishes a private bundle containing
the alias, observation time and exact readings. Use a local folder with private
permissions. Redirects and inherited HTTP proxies
are disabled. The tool reconstructs pagination requests on the documented API host.
An access refusal, incomplete cycle or conflicting meter leaves the readings file
absent. An existing file is preserved. After validation, the binding is published
before the readings, so a failed write can be retried with the same meter.

### Schedule account readings for recurring bills

Use the installed account capture tools. First complete an interactive capture
in this workspace to verify and bind the meter. Enroll for a recurring seed
after confirming the intended account and meter at the hidden input prompts.
Enrollment checks the supplier account and the workspace binding, then saves
`.canary-octopus-capture.cred` next to the seed. It uses Windows current-user
DPAPI or the Linux systemd user credential broker (systemd 256 or newer).
[Windows DPAPI](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata)
[systemd user credentials](https://www.freedesktop.org/software/systemd/man/latest/systemd-creds.html#--user)

~~~text
python -I -B tools/capture_octopus_consumption.py --confirm-electricity-import --enroll-scheduled PRIVATE_DIRECTORY/march.cmr
python -I -B tools/capture_octopus_consumption.py --confirm-electricity-import --run-scheduled PRIVATE_DIRECTORY/march.cmr
~~~

The second command makes one scheduled check. Use the same seed and OS account
as enrollment. The collector leaves the credential locked while the next cycle
is open, already has staged inputs, or the watcher is publishing its archive.
When an elapsed cycle needs readings,
it verifies the encrypted seed binding and the workspace's meter binding before
contacting the supplier. With the repeat interval enabled, a valid partial
supplier count waits for the next check. Authorization and integrity failures
stop the collector for review.

Run the collector alongside the [billing watcher](#run-the-watcher-at-login).
The collector supplies readings. The watcher supplies rates if authorized and
completes the archive. Choose an interval from 60 to 86400 seconds. On Windows,
register a separate task under the workspace owner's account using the installed
`pythonw.exe`:

~~~powershell
$pythonw = (Get-Command pythonw.exe -ErrorAction Stop).Source
$task = .\deploy\Register-BillingWatch.ps1 -Name home-capture -Seed 'D:\Bills\march.cmr' -Seconds 300 -AccountCapturePython $pythonw
Start-ScheduledTask -TaskName $task
~~~

On Linux, render a separate user unit and enable it after inspection:

~~~bash
mkdir -p ~/.config/systemd/user
(
  set -o noclobber
  ./deploy/billing-watch.sh "$seed" 300 --account-capture /usr/bin/python3 > ~/.config/systemd/user/canaryminer-octopus-capture.service
)
systemctl --user daemon-reload
systemctl --user enable --now canaryminer-octopus-capture.service
~~~

Stop the collector before moving the workspace, changing its seed or rotating
credentials. Keep the lease file in place while the collector runs. To rotate,
move the encrypted credential aside in the private workspace, enroll the
replacement, and restart the collector after successful verification. Keep the
meter binding with the archive chain. Remove the separate task or user unit
when scheduled account access is no longer wanted.

### Review cumulative meter readings

Use the same command with an interval file in schema version 2:

```json
{
  "schema_version": 2,
  "scope": "synthetic-whole-meter-scenario",
  "register_readings": [
    {"utc_ms": 1711886400000, "imported_kwh": "248129.75"},
    {"utc_ms": 1711887300000, "imported_kwh": "248130.01"}
  ]
}
```

Provide 2–8,193 strictly ordered timestamps from one whole-meter import register, or up to 18,433 for an annual policy or a demand charge. The first timestamp
must match the saved opening's through-time. It anchors subsequent differences. The saved opening
supplies the cycle's accrued kWh and subtotal separately. Values are canonical, nonnegative decimal
strings in kWh, with up to 38 digits and 18 decimal places. A repeated register value records zero import.
Timestamp duplicates, decreasing readings, meter replacements and register rollovers require a
corrected source file or a new review with a corresponding opening.

A positive difference is priced once at the energy rate and tier schedule that apply to its entire
interval. A stable energy rate can cover a civil midnight. Daily fees still follow the saved tariff.
If a positive interval contains a base rate or progressive tier price change, supply a reading at
that change.
Always supply readings at billing-cycle boundaries inside the review. The report uses exact
differences and rejects an incomplete review before printing cycle amounts. The same historical
cutoff, twelve-cycle limit and private archive replay apply.

### Review prepaid purchases

Run `canary-miner meter-bill --tariff TARIFF.json --intervals METER.json --prepaid PREPAID.json`.
The prepaid file records purchases for the same meter and currency as the saved tariff.
Use cumulative import readings (versions 2, 5 or 7) with an exact reading at every
purchase time. Consumption up to that time uses the previous token inventory. A
purchase at that time becomes available for the next interval. A review that would
overdraw the inventory stops without producing an estimate or archive.

```json
{
  "schema_version": 1,
  "scope": "synthetic-whole-meter-scenario",
  "currency": "USD",
  "published_utc_ms": 1705276800000,
  "purchase_reset": {"monthly_day": 1, "clock": {"kind": "utc", "fixed_offset_seconds": 0}},
  "fee_clock": {"kind": "utc", "fixed_offset_seconds": 0},
  "fee_per_civil_day": "1",
  "bands": [
    {"through_kwh": "100", "rate_per_kwh": "1"},
    {"through_kwh": null, "rate_per_kwh": "2"}
  ],
  "opening": {
    "through_utc_ms": 1705276800000,
    "purchased_in_cycle_kwh": "0",
    "token_inventory_kwh": "5",
    "unpaid_fee": "0"
  },
  "purchases": [
    {"utc_ms": 1706140800000, "kwh": "60", "cash_paid": "70"},
    {"utc_ms": 1706572800000, "kwh": "60", "cash_paid": "85"}
  ]
}
```

Enter cumulative purchases in the current purchase period, usable inventory
in kWh, and unpaid daily fees as of the saved opening. Include a purchase at the
opening itself in those opening amounts. List up to 256 later purchases in strict UTC
order and within the meter coverage. The 64 KiB file uses nonnegative decimal strings.
Purchase quantities must be positive. Progressive thresholds strictly increase, with
an unbounded final band. The policy's publication time must be at or before the
review cutoff. The monthly reset uses the specified clock and clamps dates past month end.

The purchase reset starts a fresh purchase quantity without clearing inventory or fees.
Supply cycles, annual accrual and demand histories use the tariff's own calendar. A
purchase charges the incremental progressive block cost and collects fees for the
civil dates that ended since the previous purchase or the opening. The fee clock may
use a different reviewed UTC, fixed-offset or named IANA clock. A purchase at midnight
includes the date just ended. A period with no purchase retains accrued fees through
the last meter reading.

The report gives acquisition charges, fees, declared cash paid and remaining token
inventory. Supply estimates and optional supply-invoice comparisons remain separate.
The review covers T07 and T26–T27 for explicitly declared terms. Supplier settlement,
tax rules, fee forgiveness, debt and cash wallet transfers require additional evidence.

With `--save-review NAME.cmr`, the archive retains the meter, tariff and prepaid
inputs. `--open-review` replays them. When extending it, supply the full purchase history
with `--prepaid NEW.json` alongside the full meter history. Changing the prepaid terms,
opening amounts or previously covered purchases requires a fresh review.

### Round cumulative readings for half-hour billing

Select the `rounding` field under **Plan** in Settings and press Enter to switch between display-only rounding and half-hour meter rounding. In a tariff file, the latter is `"rounding": "meter_import_half_hour_kwh_half_even_2"`. The opening, review endpoint and each cycle boundary must be on UTC half hours. Provide a reading at each UTC half-hour boundary. With demand or scheduled exports, finer readings are allowed and a zero import delta may cross a half-hour boundary. Import rates must agree throughout a nonzero half hour. The regular calendar and time-zone rules still select the applicable rate and daily fee.

The review rounds each UTC half hour's total kWh to the nearest 0.01 before applying its rate. With a demand charge, supply readings at both grids wherever a boundary splits nonzero import. Demand uses the original physical differences. Energy sums the differences within each half hour and rounds the sum once. A zero delta can cross either grid. At a tie it chooses the even hundredth. For example, 0.015 and 0.025 each bill as 0.02 kWh. The opening and ending register quantities still use the exact differences. The report shows physical import and billed usage for the reviewed intervals, and the archive checks both on replay. [Octopus Energy's documented consumption rounding](https://developer.oegb-kraken.energy/guides/rest/api-endpoints/#consumption-endpoints) is a reference for the rule.

Changing the rule during a cycle clears the accrued subtotal and requires a new reviewed value. It preserves the physical import. The option applies with no tiers to cumulative whole-meter register files and prepared Octopus import bundles. Average-watt files, Dashboard scenarios, and the durable fixture billing ledger require display-only rounding. Existing declarations keep their saved rule. Select display-only rounding and review the opening before using the declaration in a release that predates this option.

### Review publicly listed Octopus Agile prices

Use a package with the reviewed IANA data and a schema 4 declaration. The
[March 2023 example](../config/tariff-declaration.octopus.example.json) covers a complete London civil month.
Choose the dated Agile import product, its one-letter tariff region in the declaration's territory field, and
the payment method. Use GBP, the Europe/London clock, half-hour register rounding and one dynamic revision with
empty prices and a zero daily fee. Use a subtotal multiplier of 1, with no tiers, cycle charge or minimum.
The revision starts and ends at London local midnight and covers the reviewed billing cycles. For a complete
cycle, set the opening to the cycle start with zero accrued usage and charges, then supply adjacent UTC half-hour
register readings through the close. Use the same scope in both files.

Run the capture once from a trusted local directory using a build with public HTTPS and the bundled tariff time
database. Give the capture a new ASCII filename in that directory. The tariff's single empty dynamic revision
defines the elapsed London-midnight window, its dated product and its region.

```text
canary-miner meter-rates --allow-public-rates --tariff config/tariff-declaration.octopus.example.json --payment-method DIRECT_DEBIT --save-rates march-rates.json
canary-miner meter-bill --tariff config/tariff-declaration.octopus.example.json --intervals register-march.json --octopus-rates march-rates.json --save-review march.cmr
canary-miner meter-bill --open-review march.cmr
```

The capture sends the tariff product, region and UTC window to the unauthenticated standard-unit-rate and
standing-charge endpoints. It saves the original page contents with the tariff identity, payment choice and local
observation time after validating complete price coverage. The payment method is selected locally. The capture
sends no meter readings, account credentials or tariff declaration. It requests page numbers from the fixed host
and uses one two-minute budget for both price series. An invalid or incomplete response leaves no saved rate file.
When the published rates disagree with a declared accrued opening, reconcile the opening before capturing again.
If a file with the same name exists, the command preserves it. The optional --dns-server applies only to this
capture. Repository users can still produce the same format with tools/capture_octopus_rates.py.
[Octopus API price endpoints](https://developer.oegb-kraken.energy/guides/rest/api-endpoints/#price-endpoints)

The offline review checks that every half hour has one applicable price and every London day has one applicable
standing charge. It applies the prices including VAT, including negative energy prices. An ambiguous payment
row, missing page or midday fee change withholds the review. The file limit is 2 MiB and 8,192 listed unit
prices. Capture only elapsed periods and use a review cutoff at or after the observation timestamp printed in the
report. A later capture cannot supply prices to an earlier historical cutoff. The report estimates costs from the
supplied files. Supplier publication time requires separate evidence.

When saving, the archive also keeps the captured pages byte for byte and replays them together with the declaration
and readings. Archives made with captured rates require a version of CanaryMiner with import support for that product.

### Review publicly listed Octopus Go prices

For a dated `GO-VAR-YY-MM-DD` product, start with the same London clock, rounding, opening and empty dynamic
revision as for the Agile example. Set the declaration's `plan.product` to the actual Go product code.
For the sample London window, set it to `GO-VAR-22-10-14` and save the declaration as `go-tariff.json`.

```text
canary-miner meter-rates --allow-public-rates --tariff go-tariff.json --payment-method DIRECT_DEBIT --save-rates go-rates.json
canary-miner meter-bill --tariff go-tariff.json --intervals register-march.json --octopus-rates go-rates.json --save-review go.cmr
canary-miner meter-bill --open-review go.cmr
```

Go unit prices can span several hours. The review uses the published UTC start and end of each price, including
intervals that extend past either end of the review. This preserves actual day and night boundaries across
clock changes. Every reviewed half hour needs one applicable unit price. A standing charge can change at
London midnight within an active unit-price interval. The review applies the new daily fee from that midnight.
[Octopus API guidance for Go prices](https://developer.oegb-kraken.energy/guides/rest/api-endpoints/#go-prices)

### Review saved Octopus electricity import pages

Use all JSON pages from one whole-meter **electricity import** export, in pagination order. Use the UTC bounds
of the window you want to review and match the start to the tariff opening's through-time. This path accepts
half-hour records only. Octopus export and gas responses can use the same fields. Confirm the meter type before
preparing the bundle. [Octopus consumption format, resolution and billing rounding](https://developer.oegb-kraken.energy/guides/rest/api-endpoints/#consumption-endpoints)

```text
python tools/prepare_octopus_consumption.py --confirm-electricity-import --scope synthetic-whole-meter-scenario --begin 2023-03-01T00:00:00Z --end 2023-03-31T23:00:00Z --output march-usage.json page-1.json page-2.json
canary-miner meter-bill --tariff config/tariff-declaration.octopus.example.json --intervals march-usage.json --octopus-rates march-rates.json --save-review march.cmr
canary-miner meter-bill --open-review march.cmr
```

Pass every saved page in the first command. The two filenames in the example illustrate pagination. Use a
private local directory and match the alias to the saved tariff opening. The tool checks that pagination links
refer to the same electricity meter, drops those links, and writes a new schema 3 bundle. It retains interval times
and exact reported quantities and records the UTC assembly time as the observation. It preserves the source pages.
Use a review cutoff at or after that observation. The tool makes no network request and takes no credentials.

`meter-bill` requires a declared Octopus Energy tariff in GBP with half-hour rounding. It accepts UTC and signed
offset timestamps, verifies that the declared window contains each elapsed half hour once, and uses the API
quantities as reported at 0.001 kWh resolution. The existing billing rule rounds each half hour to the nearest
even hundredth at a tie. A gap, duplicate or grouped record withholds the review. With Agile or Go, supply
the corresponding public price bundle described above. A private archive keeps the prepared consumption bundle
and the calculated amounts for offline replay. Source account identity, observations before assembly, and
agreement with a supplier invoice require separate evidence.

### Compare reviewed cycles with supply charges

Add `--invoice FILE` to compare the calculation with electricity supply charges taken from an invoice. The
comparison covers each complete reviewed cycle from a zero opening to its closing time. Use the same meter alias,
currency and exact UTC cycle bounds as the tariff declaration. List one charge per reviewed cycle in chronological
order. Enter the supply charges represented by the tariff, including any standing charges and taxes that the
declared plan includes. Keep prior balances, payments, export credits and unrelated charges outside the amounts.

```json
{
  "schema_version": 1,
  "scope": "synthetic-whole-meter-scenario",
  "currency": "GBP",
  "decimal_places": 2,
  "rounding": "half_even",
  "cycles": [
    {"begin_utc_ms": 1677628800000, "end_utc_ms": 1680303600000, "supply_charge": "18.55"}
  ]
}
```

Select the invoice precision explicitly. The comparison accepts 0 to 6 decimal places and either `half_even`
or `half_away_from_zero` for ties. Charges can be signed. The command rounds the exact calculated total once
at that precision and subtracts the reported charge. A mismatch is a completed comparison, shown per cycle
with a signed difference. Invalid fields, a different meter or currency, and partial cycles produce no report.
The file limit is 16 KiB. Use a local alias and put only the fields shown above in the file.

```text
canary-miner meter-bill --tariff go-tariff.json --intervals march-usage.json --octopus-rates go-rates.json --invoice march-charges.json --save-review compared.cmr
canary-miner meter-bill --open-review compared.cmr
canary-miner meter-bill --open-review go.cmr --invoice march-charges.json
```

A version 3 archive keeps the exact invoice input and comparison along with the meter review. Reopening it
recalculates both at the original meter cutoff. To compare an older archive that contains only a meter review,
supply the invoice separately. This comparison is read-only and uses the archive's original meter cutoff. The
supplied charges remain a local declaration, independent of the historical cutoff for meter and price data.

### Reconcile interim supply charges at annual close

Pass invoice schema version 2 to `meter-bill --invoice FILE` with a reviewed annual reset policy. Cover every selected annual cycle from the zero opening through its exact close. Use the same meter scope and currency as the tariff declaration. List interim electricity supply charges in order, with exact adjoining UTC bounds that cover the year. A period with no posted charge needs an explicit zero. The dates describe the supplied statements. They do not reset the annual tier or dictate a billing cadence.

Each amount carries the chosen invoice precision and can be signed. Use only the interim charges that count toward this year's supply settlement. Exclude payments, deposits, unrelated services and any annual adjustment already represented in `true_up_charge`. Set `true_up_charge` to `null` to calculate an estimate without a stated adjustment.

```json
{
  "schema_version": 2,
  "scope": "synthetic-whole-meter-scenario",
  "currency": "USD",
  "decimal_places": 2,
  "rounding": "half_even",
  "cycles": [{
    "begin_utc_ms": 1704067200000,
    "end_utc_ms": 1735689600000,
    "interim_supply_charges": [
      {"begin_utc_ms": 1704067200000, "end_utc_ms": 1719792000000, "supply_charge": "60.00"},
      {"begin_utc_ms": 1719792000000, "end_utc_ms": 1735689600000, "supply_charge": "60.00"}
    ],
    "true_up_charge": "20.00"
  }]
}
```

The calculation rounds the annual estimate once at the invoice precision, sums the stated interim charges exactly, then subtracts that sum. For an annual estimate of 140.00 and interim charges totaling 120.00, the estimated adjustment is 20.00. A negative amount represents an estimated credit. When a stated adjustment is present, the report shows the signed difference. Incomplete cycles, gaps and overlapping statements withhold the comparison.

The invoice file limit stays at 16 KiB. `--save-review` retains the exact statement and per-period results in the private archive. `--open-review` rechecks the annual estimate and reconciliation without the original files. Earlier readers require an update to open archives containing version 2 invoice data. When extending a review, the previous archive retains its reconciliation. In an extension with a new reconciliation, every reviewed annual cycle must be complete and appear in the supplied statement.

### Compare separate export payments with an invoice

Use invoice schema version 3 with a fixed or scheduled export tariff. Enter the supply
charge and the export payment for each complete reviewed cycle. Both statements must
cover the same meter, currency and UTC cycle bounds. An export payment has a positive
sign when the supplier pays you and a negative sign when the supplier charges you.
Use an explicit zero when the statement shows no payment. Exclude unrelated credits,
account balances and payments.

```json
{
  "schema_version": 3,
  "scope": "synthetic-whole-meter-scenario",
  "currency": "USD",
  "decimal_places": 2,
  "rounding": "half_even",
  "cycles": [{
    "begin_utc_ms": 1704067200000,
    "end_utc_ms": 1706745600000,
    "supply_charge": "18.55",
    "export_payment": "-1.25"
  }]
}
```

The comparison rounds each calculated component once at the selected invoice precision.
It derives the invoice and calculated net costs by subtracting their respective export
payments from supply charges. The displayed net difference can be zero while both
components differ. The ordinary meter estimate keeps its own precision.

Use `--invoice FILE` for a new review or to compare a meter-only archive. A saved
comparison binds the original invoice and all three amounts for each cycle. To
compare an extended archive, provide a new invoice document covering all cycles in
the extension. The original archive remains available for its own statement. Earlier
readers need an update to open an archive with this invoice format. This format
requires both monetary components to share a billing cycle.

### Compare independent supply and export statements

Use invoice schema version 4 when the supply and export providers close their
statements on different dates. Each group has its own precision and rounding.
Use `half_even` or `half_away_from_zero` with zero to six decimal places.
Supply and export must refer to the meter scope and currency in the tariff.

```json
{
  "schema_version": 4,
  "scope": "synthetic-whole-meter-scenario",
  "currency": "USD",
  "supply": {
    "decimal_places": 2,
    "rounding": "half_even",
    "periods": [
      {"begin_utc_ms": 1704067200000, "end_utc_ms": 1706745600000, "supply_charge": "18.55"}
    ]
  },
  "export": {
    "decimal_places": 3,
    "rounding": "half_away_from_zero",
    "periods": [
      {"begin_utc_ms": 1705276800000, "end_utc_ms": 1707955200000, "export_payment": "-1.250"}
    ]
  }
}
```

List periods in time order within each group. They may have gaps. Either group
can have an empty list if you only have one kind of statement. Each supply
statement must match a complete reviewed tariff cycle from a zero opening.
Export statements require a declared export payment. Each export statement
must fit within the supplied readings and can cross a supply reset. The review
prices export at the tariff's fixed rate or by its effective UTC rate intervals.
The tariff and meter data must support the ordinary estimate over the
submitted readings. Signed average-power intervals can be split at statement
boundaries. For an increasing cumulative export register, provide a reading
at each statement boundary. A constant export register establishes zero export
throughout the interval.

The report shows a difference for each statement. It shows a net statement
difference only when both groups cover the same continuous period. It sums
their individually rounded amounts before subtracting export from supply.
The ordinary meter estimate uses its original supply cycles and precision.

Use `--invoice FILE` with new readings or a meter-only archive. The maximum
invoice size is 16 KiB, with up to 12 supply and 128 export statements. A saved
comparison retains the original invoice and exact per-period calculation.
Earlier versions of the miner require an update to reopen a saved schema 4
invoice. To compare an extended archive, provide the statements you want to
compare with the extended readings.

### Save and reopen a local meter review

Add `--save-review NAME.cmr` to a successful `meter-bill` review to save a new archive in the current directory. Use an ASCII basename ending in `.cmr`. Run the command from a private, trusted local directory. The archive contains the original tariff declaration and all supplied meter readings, so it carries the same privacy considerations as those files.

```text
canary-miner meter-bill --tariff tariff.json --intervals readings.json --as-of-utc-ms 1711936800000 --save-review april.cmr
canary-miner meter-bill --open-review april.cmr
```

The archive keeps both input files byte for byte, the effective review cutoff, and the exact per-cycle usage and amount records. With no explicit cutoff, the command records the local UTC time sampled at the start of the review. Opening uses that saved cutoff and those exact inputs, verifies the archive checksum and recalculates the amounts before displaying the estimate. The original input files can move or change. A checksum detects damage and supports replay checking. It carries no supplier or meter authentication.

Saving uses an exclusive, synced sibling file and publishes a complete archive without replacing an existing file. A save cancelled before publication or an interrupted verification returns exit status 130. A process interruption can leave a `.cmr.tmp` sibling. Its presence blocks a new save under that name. Reopen the archive to verify its outcome and choose a new name for another save. On Linux the local filesystem must support hard links. File sync can fail or stall, and this path has no physical power-loss certification. Opening is read-only. The archive preserves an estimate from imported readings and a declared tariff. Runtime billing, observed-source admission and economic-mode activation have separate requirements.


## Persistence, privacy and rollback

The file is limited to **64 KiB**, 16 revisions, 32 calendar rules/bands per revision, 256 dynamic rows per revision and 512 total rows: narrower than the pure API. All nested keys/types/bounds are strict. Invalid/newer/unknown/nonregular files are preserved; failed loads disable Save. Missing files return a blank draft without creating data.

An OS lease and byte-for-byte loaded-target comparison reject stale/concurrent writes. Exclusive temporary creation, file sync and replacement follow the local-preference pattern; POSIX also syncs the parent directory. If directory sync fails after replacement, the store says **may be saved**, invalidates its loaded state and requires reopening; it never claims rollback. POSIX new files are 0600; Windows inherits the private parent ACL.

Identity/rules/clock/source changes require a new `plan.key`; opening or plan-binding changes require a new `opening.key`. This compares the **loaded local target**, not an authenticated global version registry. External edits or moved older files can reuse historical keys; a future immutable billing ledger must enforce retained-version binding itself.

Opening the page loads only this file. Save is synchronous and size-bounded, **not bounded against genuine disk/kernel stalls**. Physical power-loss/fault injection, hostile-directory races, multi-host filesystems and hard-stop deadlines during blocked storage remain unqualified. Render callbacks perform no I/O or bill evaluation. Save changes no runtime tariff, collector, device controls or session intent.

For rollback to a build that accepts only v1 declarations, keep a copy of the v2 file and use the current editor
to return to explicit dates before downgrading. The older v1 editor rejects and preserves a v2 file. Runtime files,
paths, source references, private tariffs and measurements are excluded from public contribution evidence.

## Verification and remaining work

Tests cover strict schema/output preservation, TOU/progressive and signed dynamic arithmetic with hand-derived totals, continuation without duplicated fees, all-units horizon restrictions, missing prices, invalid clocks/decimals/openings, limits, stale writers/leases/version binding and failed-load preservation. Native tests cover nested editing/error focus, row add/remove/cancel, dirty leave, contextual stop keys, overflow and no-color 60/80/120-column × 12/24-row frames. POSIX PTY authors a combined example from a blank page, tests independent drafts, same-key rejection, active-session nonactivation and real editor SIGINT.

P-05v passed 1,289 declaration checks and 45 POSIX controlling-terminal checks. Windows passed 11 focused CTest groups
and an owned ConPTY run with the synthetic supervisor. The running Dashboard showed 2.42 USD without mining,
11.44 USD with mining and 9.02 USD difference. Escape discarded the result and returned to the tariff page.
The POSIX stop checks use separate state directories for Q and Ctrl+C. Each retains the saved declaration and starts
one synthetic worker. These scenarios use a synthetic tariff and synthetic workers.

P-05w passed 1,353 declaration checks on Windows and Linux, including the Linux AddressSanitizer and
UndefinedBehaviorSanitizer build. The 52 Linux terminal checks and a Windows ConPTY run exercised the
running Dashboard. The added scenarios cover the day-31 reset, independent fixed-offset clocks, per-cycle
tiers and minimums, future rate gaps and preservation of the saved opening.

P-05x passed 37 command cases on Windows and 40 on Linux. They cover native Unicode names,
whole-report refusal on missing coverage, nonlinear charges around a monthly boundary, and unchanged input files.
Linux adds symlink, FIFO and process-signal cases. Both platforms pass the deterministic cancellation check
after two completed calculation segments.

Executed development evidence (2026-10-29): Windows/MSVC focused CTest **45/45** passes, including prior product/settings/energy/credit recovery/donation/intent/market/electricity/oracle families. Final per-leaf review refinement passes the targeted tariff/settings rerun; both platforms' new native program has **1,071 passing checks**. Linux/WSL/GCC passes **16 new actual-terminal checks**, existing settings/runtime/manual-price/session-energy PTY regressions, and **1,071 own-code ASan/UBSan checks**. Linux Python discovery passes **123 tests with seven explicit fixture-dependent skips**. Windows full Python discovery/GNU-toolchain/encoding limitations remain unqualified.

GCC's reference-lifetime diagnostic led to splitting bounded-array validation from direct DOM iteration, without suppressing the warning. The first PTY fixture used a fixed publication instant later than the test machine's confirmation; Save correctly rejected it. The fixture now shifts only its invented instants into the past. Neither was treated as provider/production evidence.

Reproduction (host-only; build path may be selected locally):

```text
cmake --preset tests
cmake --build --preset tests --target test_tariff_declaration test_product_settings test_pearl_cuda_cli_host test_product_supervisor
ctest --preset tests -R "^(tariff_declaration|product_settings|tariff_declaration_terminal)$" --output-on-failure
python3 -B tests/test_config_schema.py
python3 -B tests/test_build_natives_recipe.py
python3 -B tests/test_tariff_declaration_terminal.py <host-cli-fixture> <host-supervisor-fixture>
```

Linux development builds used direct GCC (`-std=c++17 -O1 -Wall -Wextra -Werror -pthread`) over tariff-declaration/electricity/civil-time/exact-decimal sources; sanitizer builds add `-g -fsanitize=address,undefined -fno-omit-frame-pointer`. This is not Linux CMake qualification. Host/PTY checks do not qualify supplier, native GPU, household/meter or physical durability behavior. The legacy compile-only recipe links tariff/time/decimal objects as host code; it does not qualify CUDA/SASS itself. Full pinned-toolchain/verifier/protocol/SASS, native GPU/game, named-zone packaging, hardware/driver stalls and physical-storage qualification remain unrun for this slice.

Remaining: isolated durable observed bill/energy state, independent reset calendars, capable reviewed supplier adapters, qualified whole-meter/baseline observations, authenticated named-zone packaging, provider-qualified credit/reward/FX forecasts, runtime economic integration, profiles/optimizer/game/TLS/i18n and release gates. P-05/P-08 and the overall planning-book objective remain partial.

[P-13at](tariff-notice-messages.md) later extracts eleven local input/draft/save notices with exact native English and separate
typed drafts. The original editor/validator/store/JSON/review and no-runtime-activation contract remain; printed outcomes and
mock callbacks are not persisted receipts, supplier approval or billing/reset permission. Historical P-05c evidence is unchanged.

[P-13au](tariff-panel-messages.md) subsequently extracts literal shell/review limitations with unchanged native frames and
JSON. Descriptions remain distinct from model/parser/store enforcement and do not activate or qualify actual billing.

[P-13av](tariff-hint-messages.md) later extracts editor guidance without changing field matching, native English, JSON or
validation. Displaying syntax, unit or clock guidance neither admits a declaration nor supplies supplier/source facts.

[P-13aw](tariff-context-messages.md) later extracts editor/path/position envelopes with unchanged native raw originals and
indices. Independent typed display bounds do not alter editor/parser/store admission or supply numeric/path/source truth.

[P-13ax](tariff-value-messages.md) later extracts complete browse rows without changing JSON tokens or fixed-field/prompt
semantics. Opaque original tokens are not localized human quantities, disclosure authority or parser/model admission.

[P-13ay](tariff-review-messages.md) later exposes display-neutral private change records without model UI imports, preserving
legacy strings and native complete review. Original JSON/path and row counts remain distinct and grant no fact/disclosure
authority; parser/store and offline-only save semantics are unchanged.

[P-13az](tariff-problem-metadata.md) later adds display-neutral refusal kinds and ordered choice/typed model-status payloads.
All legacy refusal paths/messages/priority, raw native rendering and offline save contracts remain; metadata is not approval
or a translated/effective UI.

[P-13ba](tariff-diagnostic-messages.md) later extracts classified refusal descriptions through explicit UI-only kind mapping
and exact source equality. Native English/raw unknown/model/changed outcomes and parser/store/offline-save semantics remain;
choice display supplies no enum admission or supplier/source truth.

[P-13bc](tariff-error-messages.md) later extracts complete path/root/raw error envelopes with exact source English and
unparsed callback preservation. Independent typed original/human roles are display contracts, not sanitizer, disclosure,
classification or model/action authority; native save/validation/stop behavior remains unchanged.

[P-13bb](electricity-status-messages.md) later adds human model-status descriptions behind explicit kind/payload/source
equality in UI. Core raw strings and native path/message/validation/offline-save behavior remain; labels prove no arithmetic,
coverage, supplier or source fact and do not activate translated native UI.
