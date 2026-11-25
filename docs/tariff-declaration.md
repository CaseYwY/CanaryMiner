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
4. Version 1 uses `explicit_cycle_only`. Version 2 defines a monthly whole-meter cycle. Version 3 adds explicit daily-fee allocation to the monthly cycle. Version 4 supports named clocks. Version 5 supports an annual cycle. Version 6 defines a two-month cycle using monthly-average all-units pricing. Version 7 adds seasonal progressive allowances. The rounding default is `final_display_only`. All versions share the basis, origin, time-version and acknowledgement rules. Rate changes do not reset shared quantity.

The decoder calls `validateTariffOpening`, not a fake zero-power bill. This proves **structure/binding**, not sufficient price/trace coverage or a forecast. Evaluation still requires complete bound trajectories, prices published at the decision instant, unambiguous calendar winners and a supported horizon. Dynamic gaps remain missing; overlapping records reject. Minimum/all-units need the full remaining cycle. Saving does not assert that a future decision can be priced.

A saved opening is an offline scenario assertion, **not** an observed accumulator, recovered bill, source cursor, meter calibration or permission to reset an actual ledger. Qualified whole-meter/baseline telemetry and an isolated durable state owner remain runtime prerequisites. Editing the declaration does not rewrite measured history.

Unsupported families remain explicit: demand/capacity/ratchets, export/netting/opportunity, prepaid/inventory/debt, independent supplier tariff registers and automatic true-up, other proration, threshold revisions, other provider rounding rules and ordered/selective taxes. Synthetic examples are not official supplier certification.

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

Open Combined tariff from the Dashboard Settings or the idle `settings` command and press **P**. Enter the constant whole-meter import watts
for the **without mining** and **with mining** scenarios. Use **Tab** to change fields and **Enter** to calculate.
The calculation uses the saved plan and opening, including any declared accrued usage. Each entered watt value
applies to the full displayed UTC interval from the opening's through-time to the selected end.

For a saved monthly, annual or two-month reset rule, use **Left/Right** to select 1–12 cycles. The first selection is the remainder of
the saved cycle. Each additional cycle runs from one declared reset to the next. The same entered watts apply
through the displayed end. Later cycles start with zero use and zero accrued subtotal, and each cycle applies its
own fees, tier allowance and minimum. Charges are added exactly before display rounding. A declaration with
explicit cycle dates still has one horizon.

Every selected cycle needs complete rates known at the original comparison time. A missing rate in any selected
cycle withholds all three amounts. The first cycle uses the saved opening's daily-fee rule. Without allocation, the comparison reports the first later cycle that splits a charged tariff day. With the declared allocation, it splits the daily fee across the affected cycles. Selecting a horizon applies only to the estimate.

The amounts are the hypothetical remaining charges for both scenarios and their difference, in the plan currency.
The two scenarios share one opening and the same known tariff revisions. An incomplete rate schedule produces an
unavailable result. A background calculation uses the inputs and local time captured at Enter. Escape returns to the
tariff declaration and cancels the calculation. The new comparison starts with empty inputs. If a cancelled calculation
is still exiting, press Enter again when prompted. The session stop path revokes the calculation before draining workers.

The comparison is an estimate from the saved declaration and entered watts. It has no observed usage or economic-mode permission.

## Review a local meter file

Run `canary-miner meter-bill --tariff tariff-declaration.json --intervals meter-intervals.json` with an explicitly chosen
saved tariff and source file. The command reads those files and prints the review to stdout. Its estimates remain local
to the command and include any declared opening used for the first cycle.

The interval file has schema version 1. Use the same scope as the tariff opening and supply ordered, adjoining
whole-meter import intervals. Each interval uses UTC milliseconds and a canonical nonnegative decimal string in watts.

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

Regular meter files for an annual policy can contain up to 1.75 MiB and 18,432 intervals. Other policies retain 1 MiB and 8,192 intervals. A review can cover up to twelve cycles. Coverage gaps, overlap, wrong scope, future readings,
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

Provide 2–8,193 strictly ordered timestamps from one whole-meter import register, or up to 18,433 for an annual policy. The first timestamp
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

### Round cumulative readings for half-hour billing

Select the `rounding` field under **Plan** in Settings and press Enter to switch between display-only rounding and half-hour meter rounding. In a tariff file, the latter is `"rounding": "meter_import_half_hour_kwh_half_even_2"`. The rule requires readings on every UTC half hour, including the opening, review endpoint and each cycle boundary. Rates must agree throughout a nonzero interval. The regular calendar and time-zone rules still select the applicable rate and daily fee.

The review takes the difference between adjacent cumulative readings and rounds each interval's kWh to the nearest 0.01 before applying its rate. At a tie it chooses the even hundredth. For example, 0.015 and 0.025 each bill as 0.02 kWh. The opening and ending register quantities still use the exact differences. The report shows physical import and billed usage for the reviewed intervals, and the archive checks both on replay. [Octopus Energy's documented consumption rounding](https://developer.oegb-kraken.energy/guides/rest/api-endpoints/#consumption-endpoints) is a reference for the rule.

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

Capture the public rate pages once from the repository with Python 3.8 or later. Give the capture a new filename
in a trusted local directory and use the UTC instants for the local window boundaries (inclusive start, exclusive end).

```text
python tools/capture_octopus_rates.py --product AGILE-FLEX-22-11-25 --region C --payment-method DIRECT_DEBIT --begin 2023-03-01T00:00:00Z --end 2023-03-31T23:00:00Z --output march-rates.json
canary-miner meter-bill --tariff config/tariff-declaration.octopus.example.json --intervals register-march.json --octopus-rates march-rates.json --save-review march.cmr
canary-miner meter-bill --open-review march.cmr
```

The capture uses the unauthenticated standard-unit-rate and standing-charge endpoints and saves the original page
contents along with the tariff identity, payment choice and local observation time. Meter readings stay local.
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
python tools/capture_octopus_rates.py --product GO-VAR-22-10-14 --region C --payment-method DIRECT_DEBIT --begin 2023-03-01T00:00:00Z --end 2023-03-31T23:00:00Z --output go-rates.json
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
