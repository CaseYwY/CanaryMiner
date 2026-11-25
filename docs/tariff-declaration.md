# Native combined tariff and declared opening (P-05c)

**S → Electricity → Combined tariff / declared whole-meter opening** edits and saves the supported [pure electricity model](electricity-core.md). The idle `settings` command exposes the same page. It is an **offline declaration**, not an enabled electricity collector, measured bill/reset ledger, supplier-verified adapter or optimizer. The [session GPU-board flat-energy model](session-energy.md) keeps its own controls/save target.

Requirements R-UI-007/008, R-TAR-001–006 and R-PRIV-002 remain partial. Settings saves a **user-declared opening scenario** for the composable tariff. Measured state transitions and separate allowance resets remain unfinished.

## Native editing and review

- Arrows/Tab select fields. Enter opens a section/table or edits a value; Space toggles acknowledgement. Esc cancels an edit, returns to the parent, then offers review/discard/continue on dirty exit. Q/D/L/S inside editors are text; Ctrl+C stops. No browser/GUI runtime is added.
- A appends a **blank** revision/calendar/dynamic/band row; X confirms removal from the draft. Changing a rule mode never clears an incompatible table automatically; validation fails until explicitly edited.
- Empty monetary/quantity text is missing, not zero. Cycle/day fees, multiplier and opening amounts require explicit entries, even when zero/one. Canonical signed decimals allow at most 38 coefficient digits / 18 fractional places. No exponent, float storage or intermediate rounding. Clearing an optional minimum/final band/date chooses `null`, distinct from zero.
- Clocks are UTC or whole-minute fixed STANDARD offsets within +/-18 hours. No DST/OS-zone inference. Rule/opening instants use **UTC milliseconds**; local confirmation uses seconds. Revision ends are exclusive; all-units upper thresholds are inclusive.
- Field names match the schema. Hints explain enums, minute bounds, masks and units. Weekday bit 0 is Monday; month bit 0 is January. Dates use YYYY-MM-DD or null. Split overnight windows; explicit priorities resolve declared calendar events.
- V/F2 shows before/after changes, the complete draft and exclusions. Enter/F2 saves only `tariff-declaration.json`. Errors preserve drafts; field-specific errors focus the relevant field. Cross-field failures identify the plan/opening contract. UI/runtime/manual-price/flat-energy drafts save independently.
- Confirmation stamps the local action only; it cannot alter publication/effective bounds, renew a feed or authenticate a supplier. Claimed publication after confirmation is rejected. Loading never silently renews a declaration.

The supported terminal remains 60+ columns and 12+ rows. Focus precedes a long nested path; review scrolls. NO_COLOR retains textual focus. ASCII input remains explicit until Unicode/RTL work. This does not certify screen-reader access or sub-60 layouts.

## Represented contract

The strict [declaration schema](../config/tariff-declaration.schema.json) and [invented v1 example](../config/tariff-declaration.example.json) contain:

1. Independent jurisdiction, territory, supplier, product, currency, plan version key, clock and unverified local source reference. Three-letter currency is an identifier, not FX/legal-tender qualification.
2. Contiguous revisions with constant/calendar/dynamic time pricing, signed rates, publication instants and additive shared-register progressive or true all-units bands. Calendar fallback/events, fees, optional minimum and one whole-subtotal multiplier retain the pure core's exact documented order.
3. Separate opening key, whole-meter scope/adapter declaration, cycle begin/end/through, accumulated import kWh and **pre-multiplier accrued subtotal**. Origin is always **declared forecast**; typing a meter label cannot create observed telemetry. At cycle start both amounts must explicitly be zero; mid-cycle requires caller-supplied amounts and the clock version.
4. Version 1 uses `explicit_cycle_only`. Version 2 defines a monthly whole-meter cycle. Version 3 adds an explicit daily-fee allocation to that monthly cycle. All three use `final_display_only` rounding and the same basis, origin, time-version and acknowledgement rules. Rate changes do not reset shared quantity.

The decoder calls `validateTariffOpening`, not a fake zero-power bill. This proves **structure/binding**, not sufficient price/trace coverage or a forecast. Evaluation still requires complete bound trajectories, prices published at the decision instant, unambiguous calendar winners and a supported horizon. Dynamic gaps remain missing; overlapping records reject. Minimum/all-units need the full remaining cycle. Saving does not assert that a future decision can be priced.

A saved opening is an offline scenario assertion, **not** an observed accumulator, recovered bill, source cursor, meter calibration or permission to reset an actual ledger. Qualified whole-meter/baseline telemetry and an isolated durable state owner remain runtime prerequisites. Editing the declaration does not rewrite measured history.

Unsupported families remain explicit: demand/capacity/ratchets, export/netting/opportunity, prepaid/inventory/debt, independent resets/annual true-up, other proration, threshold revisions, provider intermediate rounding and ordered/selective taxes. Synthetic examples are not official supplier certification.

## Derive a monthly whole-meter cycle

Select `reset_policy` and press **M** to choose the monthly policy. Open the policy with **Enter** and enter a day
from 1 to 31. The draft starts with the rate clock as a suggested value. Review the reset clock and edit it if your
billing cycle uses a different UTC or fixed standard offset. The saved reset clock is independent of the rate clock.
Each month starts the cycle at 00:00 on the chosen day. Short months use their last day and the original day applies
again the following month.

Enter the opening's through-time in UTC milliseconds, then press **G** to derive the dates of the cycle containing
that instant. At an exact boundary, the new cycle starts. If the start changes, clear both opening amounts with
**Backspace** in their editors, derive the dates, then explicitly enter the amounts for the new period. At the start
of a cycle, both amounts are zero. The tariff revisions must cover the full derived cycle. Use **V** to review the
policy, dates and values, then **Enter** to save. Changing the reset policy requires new plan and opening keys.

Existing declarations retain their explicit dates. Press **M** on the monthly policy to return to manually entered
dates, then review any edits before saving. The monthly rule derives dates in the draft and validates the saved
opening against them. It does not advance with elapsed time or reset a bill, readings or accrued amounts.

### Prorate a daily fee across the monthly reset

With the monthly rule selected on `reset_policy`, press **F** to toggle the daily-fee rule. Review with **V** and save with **Enter**. Enabling it uses schema version 3 and stores `"daily_fee_allocation": "prorate_by_cycle_minutes"` inside `reset_policy`. Changing the rule requires new plan and opening keys. If the opening starts after the reset, the editor clears its draft accrued subtotal. Re-enter the amount using the selected fee rule before saving.

The fee for a tariff day comes from the rate revision effective at the tariff clock's midnight. Each billing cycle receives the fee times its overlapping minutes divided by 1,440. For example, a 1.44 daily fee split at 18:30 in the tariff clock contributes 1.11 to the closing cycle and 0.33 to the new cycle. A revision at 18:30 can change the energy rate that minute. Its daily fee starts on the next tariff day. Each cycle books its share once, on first covered use or zero-usage coverage of that day. When the calculation first books a share, it requires a midnight revision published by the review cutoff. A declared mid-day opening may already include that share.

The rule uses UTC or a fixed standard offset, with whole-minute boundaries and exact arithmetic through the cycle minimum and multiplier. Midnight-based boundaries in the same clock allocate a whole day to a single cycle. Version 2 retains its existing split-day refusal in a later cycle.

## Compare two whole-meter scenarios

Open Combined tariff from the Dashboard Settings or the idle `settings` command and press **P**. Enter the constant whole-meter import watts
for the **without mining** and **with mining** scenarios. Use **Tab** to change fields and **Enter** to calculate.
The calculation uses the saved plan and opening, including any declared accrued usage. Each entered watt value
applies to the full displayed UTC interval from the opening's through-time to the selected end.

For a saved monthly reset rule, use **Left/Right** to select 1–12 cycles. The first selection is the remainder of
the saved cycle. Each additional cycle runs from one declared reset to the next. The same entered watts apply
through the displayed end. Later cycles start with zero use and zero accrued subtotal, and each cycle applies its
own fees, tier allowance and minimum. Charges are added exactly before display rounding. A declaration with
explicit cycle dates still has one horizon.

Every selected cycle needs complete rates known at the original comparison time. A missing rate in any selected
cycle withholds all three amounts. The first cycle uses the saved opening's daily-fee rule. A version 2 comparison reports the first later cycle that splits a charged tariff day. Version 3 applies the declared allocation to each cycle. Selecting a horizon applies only to the estimate.

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
uniformly within each interval. This convention matters when an interval spans a tariff change or monthly reset.
The output lists reviewed UTC coverage, imported kWh, the charge change from those intervals, and the estimate for
each touched cycle. A cycle that ends before the next reset has a provisional estimate. Subsequent monthly cycles start
with fresh allowances and fees from the saved rule. An interval that ends exactly on a reset closes the preceding cycle.

Add `--as-of-utc-ms MS` with a positive ASCII decimal UTC millisecond timestamp to replay a historical review.
The cutoff appears in the report. Every supplied interval must end at or before the cutoff. A rate published later cannot affect the estimate.
If such a rate is needed, the entire review is refused. Reset-day fee checks use the same cutoff. A future cutoff is
invalid. The saved declaration still supplies
the opening values, so the cutoff describes rate publication and reading coverage, and local confirmation can be later.
Omit the option to use the current UTC time.

The reader accepts up to 1 MiB, 8,192 intervals and twelve cycles. Coverage gaps, overlap, wrong scope, future readings,
unknown rates and unsupported reset rules withhold all amounts, including earlier cycles in the same review. An
additional cycle that starts within a charged tariff day requires the version 3 [daily-fee rule](#prorate-a-daily-fee-across-the-monthly-reset). Use regular files in a trusted
local directory. Windows accepts native Unicode paths on local drives and rejects redirected directories. The command
prints no file paths or scope labels. Ctrl+C cancels the calculation. A stalled filesystem read can delay exit.
Estimation from submitted data and qualified live collection remain separate acceptance steps.

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
