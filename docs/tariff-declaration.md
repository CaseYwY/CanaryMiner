# Native combined tariff and declared opening (P-05c)

**S → Electricity → Combined tariff / declared whole-meter opening** edits and saves the supported [pure electricity model](electricity-core.md). The idle `settings` command exposes the same page. It is an **offline declaration**, not an enabled electricity collector, measured bill/reset ledger, supplier-verified adapter or optimizer. The [session GPU-board flat-energy model](session-energy.md) keeps its own controls/save target.

Requirements R-UI-007/008, R-TAR-001–006 and R-PRIV-002 remain partial. This adds native configuration for existing composable arithmetic and a persisted **user-declared opening scenario**; measured state transitions and independent reset clocks remain unfinished.

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

The strict [v1 schema](../config/tariff-declaration.schema.json) and [invented example](../config/tariff-declaration.example.json) contain:

1. Independent jurisdiction, territory, supplier, product, currency, plan version key, clock and unverified local source reference. Three-letter currency is an identifier, not FX/legal-tender qualification.
2. Contiguous revisions with constant/calendar/dynamic time pricing, signed rates, publication instants and additive shared-register progressive or true all-units bands. Calendar fallback/events, fees, optional minimum and one whole-subtotal multiplier retain the pure core's exact documented order.
3. Separate opening key, whole-meter scope/adapter declaration, cycle begin/end/through, accumulated import kWh and **pre-multiplier accrued subtotal**. Origin is always **declared forecast**; typing a meter label cannot create observed telemetry. At cycle start both amounts must explicitly be zero; mid-cycle requires caller-supplied amounts and the clock version.
4. Fixed `explicit_cycle_only` reset policy, `final_display_only` rounding, basis/origin/time-version contracts and acknowledgement. Unsupported values/keys reject. There is no independent allowance reset, rollover or automatic next-cycle creation. Rate changes do not reset shared quantity.

The decoder calls `validateTariffOpening`, not a fake zero-power bill. This proves **structure/binding**, not sufficient price/trace coverage or a forecast. Evaluation still requires complete bound trajectories, prices published at the decision instant, unambiguous calendar winners and a supported horizon. Dynamic gaps remain missing; overlapping records reject. Minimum/all-units need the full remaining cycle. Saving does not assert that a future decision can be priced.

A saved opening is an offline scenario assertion, **not** an observed accumulator, recovered bill, source cursor, meter calibration or permission to reset an actual ledger. Qualified whole-meter/baseline telemetry and an isolated durable state owner remain runtime prerequisites. Editing the declaration does not rewrite measured history.

Unsupported families remain explicit: demand/capacity/ratchets, export/netting/opportunity, prepaid/inventory/debt, independent resets/annual true-up, proration, threshold revisions, provider intermediate rounding and ordered/selective taxes. Synthetic examples are not official supplier certification.

## Persistence, privacy and rollback

The file is limited to **64 KiB**, 16 revisions, 32 calendar rules/bands per revision, 256 dynamic rows per revision and 512 total rows: narrower than the pure API. All nested keys/types/bounds are strict. Invalid/newer/unknown/nonregular files are preserved; failed loads disable Save. Missing files return a blank draft without creating data.

An OS lease and byte-for-byte loaded-target comparison reject stale/concurrent writes. Exclusive temporary creation, file sync and replacement follow the local-preference pattern; POSIX also syncs the parent directory. If directory sync fails after replacement, the store says **may be saved**, invalidates its loaded state and requires reopening; it never claims rollback. POSIX new files are 0600; Windows inherits the private parent ACL.

Identity/rules/clock/source changes require a new `plan.key`; opening or plan-binding changes require a new `opening.key`. This compares the **loaded local target**, not an authenticated global version registry. External edits or moved older files can reuse historical keys; a future immutable billing ledger must enforce retained-version binding itself.

Opening the page loads only this file. Save is synchronous and size-bounded, **not bounded against genuine disk/kernel stalls**. Physical power-loss/fault injection, hostile-directory races, multi-host filesystems and hard-stop deadlines during blocked storage remain unqualified. Render callbacks perform no I/O or bill evaluation. Save changes no runtime tariff, collector, device controls or session intent.

Rollback: older builds ignore this additional file. Preserve it and any histories; no destructive migration/reset is supplied. Runtime files, paths, source references, private tariffs and measurements are excluded from public contribution evidence.

## Verification and remaining work

Tests cover strict schema/output preservation, TOU/progressive and signed dynamic arithmetic with hand-derived totals, continuation without duplicated fees, all-units horizon restrictions, missing prices, invalid clocks/decimals/openings, limits, stale writers/leases/version binding and failed-load preservation. Native tests cover nested editing/error focus, row add/remove/cancel, dirty leave, contextual stop keys, overflow and no-color 60/80/120-column × 12/24-row frames. POSIX PTY authors a combined example from a blank page, tests independent drafts, same-key rejection, active-session nonactivation and real editor SIGINT.

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
