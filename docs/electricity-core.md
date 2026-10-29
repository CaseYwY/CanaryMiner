# Explicit civil time, exact energy and declared tariff arithmetic

P-05a / P-08a implements **pure local host-side foundations**, not production financial activation, a tariff catalogue, certified bills, sensor collection, today's revenue forecast or an optimizer. `FinancialView.enabled` remains false by default; no supervisor calls these APIs. No personal plan, measurement, price or account was used.

The later [P-05b/P-08b native session slice](session-energy.md) separately adds reviewed **flat energy-only** declarations and a default-off streaming GPU-board model from existing telemetry. It does not activate this full-bill/counterfactual API, combined-rule settings, persistent billing state, actual-income/net forecasts or hardware qualification. Its explicit mapped-UTC guard is an adapter/model contract, not a change to this pure function's exact input-clock rules.

[P-05c](tariff-declaration.md) adds native composition and persisted declared-opening settings for this model. They remain offline; no measured accumulator, independent reset ledger or runtime financial activation is supplied.

[P-05g](billing-ledger.md) adds `accrueBill` for exact provisional prefixes and a separate fixture monetary ledger. The full-settlement restrictions below continue to apply to `evaluateBill`/`counterfactualBill`; prefix amounts are not shortened utility bills. The ledger is unused by the ordinary runtime pending source/checkpoint adaptation and isolated recovery ownership.

Code: `src/product/civil_time.*`, `src/product/electricity.*`; target: `canary_economics`. Related requirements: R-TAR-001–005, R-ENG-001; test families T-TARIFF/T-ENERGY and calendar prerequisites of T-FORECAST. All input declarations remain caller assertions, not provider qualification.

## Time ADR

- UTC and an explicitly selected **fixed standard offset** work with the dependency-free build. Named IANA zones are optional (`CANARY_ENABLE_NAMED_TZDB=OFF` by default). Locale, OS current zone, IP address, currency and territory are never consulted.
- Optional backend: Howard Hinnant `date-tz`, built with `BUILD_TZ_LIB=ON`, `MANUAL_TZ_DB=ON`, `USE_SYSTEM_TZ_DB=OFF`, `BUILD_SHARED_LIBS=OFF`, `ENABLE_DATE_TESTING=OFF`. `HAS_REMOTE_API=0` and `AUTO_DOWNLOAD=0` must be compiled into the dependency, not merely defined by its consumer. CMake checks the manual text-database interface and rejects a library exporting the remote-version API. This is a compatibility guard, not artifact authentication or a proof of absence of all possible network activity in a substituted library.
- The official v3.0.5 development source declares **CMake version 3.0.4**. The package-version floor therefore cannot authenticate the v3.0.5 source tag. Release builds must separately pin/review the exact source and flags.
- `configureCivilTimeData(absoluteDirectory, exactVersion)` explicitly configures one app-private reviewed **text** IANA package for process lifetime. Required files have type/size checks (2 MiB each, 16 MiB total, version <=32 bytes); wrong/missing versions fail closed. Successful configuration is idempotent for the same path/version; a different selection requires restart. An attempted parser initialization is not hot-reloaded after failure. No download, OS-database fallback, system timezone change, implicit home-directory lookup or registry-based current-zone selection is performed by this layer.
- The input directory must be a reviewed, immutable installation resource, **not a user-supplied parser sandbox**. Checks on required files do not authenticate content, prevent directory races, constrain all optional upstream files or make the upstream parser safe for malicious packages. Windows additionally requires reviewed CLDR `windowsZones.xml` because this date backend loads mappings even when no OS-zone lookup is requested. Those mappings do not choose the billing zone.
- A platform probe returned tzdb labels `2022g.27` from MSVC's system-backed C++20 implementation and `2026c` from GCC. Both passed New York's 23/25-hour examples; neither probe establishes current worldwide rules. The explicit private package removes silent platform-version drift. These labels describe API outputs, not a complete audit of the underlying system databases.
- Supported UTC and local-calendar range: 1970–9999; a day also needs a representable following midnight. `civilResolve` rejects folds/gaps by default. An explicit Earlier/Later resolves a fold; for a nonexistent label both choices clamp to the transition's first valid instant (reported as `Nonexistent` wall kind). Day boundaries deliberately use the first occurrence/first valid boundary. A skipped whole date returns `SkippedDate`; this is not a zero-hour ordinary day.
- `civilDay`/`civilDayAt` give `[beginUtcMs,endUtcMs)` and the database version. DST days can be 23/25 hours, half-hour shifts or skipped dates; no fixed-24h assumption. These APIs are prerequisites, not an income forecast. No leap-second billing model is supplied; intervals are POSIX UTC milliseconds.

Reference review: [date timezone API](https://howardhinnant.github.io/date/tz.html), [date source/build flags](https://github.com/HowardHinnant/date/tree/v3.0.5), [IANA release index](https://www.iana.org/time-zones), and Context7's first-party MSVC STL calendar/timezone documentation. References were retrieved on 2026-10-28; source authentication and clean-machine packaging remain release gates.

## Exact arithmetic and measurement

`ElectricityAmount` represents **numerator / 3,600,000,000**. For energy, the numerator is an exact `ExactDecimal` W·ms integral. For money, it is the corresponding rate-weighted numerator in the declared currency. This common denominator preserves nonterminating kWh without per-sample or per-component rounding. `format(places)` explicitly applies display-only half-even (0–24 places); it does not write a rounded value back into state. Arithmetic retains the existing 96-digit / 48-scale capacity; overflow/scale exhaustion rejects the result, not saturates or truncates it.

`sampleHoldEnergy` uses explicit watts, UTC millisecond instants, monotonic millisecond durations, measurement basis, origin, scope and adapter identity. It is an explicitly declared **sample-and-hold estimate**, not a claim of continuous sensor accuracy or trapezoidal integration:

1. Hold an available observation only until the following observation and only within the configured positive maximum gap (<=24 elapsed hours).
2. Clip that interval to the requested horizon. Never extrapolate after the final observation, fill a missing value with zero, bridge an excessive gap or assume a nominal cadence.
3. Both clocks must increase. A regression/duplicate fails the series; an unequal UTC/monotonic delta removes that interval and records a clock-discontinuity warning. Runtime adapters must supply a consistent mapping; this initial core does not silently tolerate or repair wall-clock drift.
4. Retain covered duration and known energy separately from optional complete energy. A complete observed zero is available; no samples/coverage is unavailable. Unknown sensor power is not zero; negative import watts are invalid rather than an implicit export model.

Bases distinguish GPU board, whole-PC wall, directly measured incremental wall and whole-meter import. Origins distinguish synthetic, observed and declared forecast traces. These labels are caller evidence, not proof of matched real hardware or games. No sensor adapter, persistence, scope aggregation or unit-string parser is activated by this batch. Public fixtures are invented.

## Supported declared tariffs

Plan key/version, territory, supplier, product, three-letter currency, clock, origin, rounding and opening state must be explicit. A three-letter currency field is an identifier check, **not** ISO entitlement, FX, minor-unit or legal-tender validation. No public supplier preset is shipped. A manually declared plan is not source-certified.

The initial composition is:

```text
subtotal = opening accrued subtotal
         + cycle fee (only once at cycle start)
         + fee per newly entered civil day
         + sum(held watts * elapsed ms * time rate) / denominator
         + shared-register tier adjustments
settled subtotal = max(subtotal, explicit minimum)   # only at cycle end, if selected
interval charge = (settled subtotal - opening accrued subtotal) * explicit subtotal multiplier
```

- Time rates: signed constant, local-calendar replacements with explicit fallback/priority, or nonoverlapping UTC dynamic records. Dynamic records are half-open; gaps/conflicts/unpublished prices fail, with no carry-forward or implicit latest revision. Callers supply an immutable corrected record set; an importer still needs provider revision/expiry/identity rules.
- Calendar rules: minute-of-day boundaries (overnight windows must be split), weekday/month masks, optional exact date, explicit priority and publication instant. Highest-priority ties conflict regardless of input order; lower-priority ties do not defeat an unambiguous higher override. Unannounced event rules cannot influence an earlier decision. This is not knowledge of future unannounced events.
- Tier component: none, progressive or **true all-units** selected by final whole-import quantity with inclusive upper thresholds. It is additive to time rates, with a single explicitly shared register. There is no hidden proportional allocation or per-TOU allowance. Rates can be signed. Thresholds cannot change mid-cycle in this implementation; a rate revision does not reset accumulated quantity.
- Progressive components can change rates at explicit revision boundaries. All-units currently requires one revision and evaluation through the affected cycle end. It reprices already accumulated quantity when necessary; it is not a smoothed marginal-rate approximation.
- Simple cycle/day charges, one explicit entire-subtotal multiplier and an optional final cycle minimum are supported. No inferred tax name, inclusion flag or order is supplied. The cycle/day fee convention is part of this declared model, not a universal utility rule. A backward calendar-date traversal is unsupported rather than double-charging days.
- Evaluations split at trace endpoints, revisions, dynamic records, civil midnights, DST transitions and both occurrences of applicable wall boundaries. Minimum/all-units need the full remainder of the cycle, not a short horizon that assumes cheap allowance remains free.

All flags for unknown rule families reject evaluation. **Not implemented:** demand/capacity/ratchets/reactive power, prepaid purchase/inventory/debt, import/export netting or lost export credits, restricted credits, independent annual/purchase/reset ledgers, seasonal allowance proration, provisional/annual true-up, all-units monthly-average normalization, tier reallocation/threshold revisions, supplier-specific intermediate rounding, ordered/selective taxes/multipliers, contracted eligibility or persistent beyond-cycle consequences. These need separate declared adapters/state, not zero-valued placeholders. Choosing a simpler energy-only scenario must be explicit and visibly exclude the omitted rules.

## Pure opening state and counterfactuals

`BillOpening` declares the immutable plan-version key, scope/basis/origin/adapter binding, exact cycle bounds, through instant, shared accumulated import quantity and **pre-multiplier** accrued subtotal. At cycle start both amounts must be zero. Later amounts are required caller assertions; no missing household history is fabricated. An ending state records its time-database version; continuation without that version or with a conflicting version fails closed. Bill results retain plan identity/origin, currency, clock/database version, evidence and decision/horizon instants. Cycles are explicit and bounded to 370 elapsed days; automatic billing/reset-calendar generation is not implemented. State is in memory only; no storage schema/config migration is introduced.

`evaluateBill` requires complete contiguous nonnegative power intervals for its entire horizon, bound to the opening state's scope/basis/origin/adapter. An observed-telemetry trace cannot extend beyond the decision instant; future trajectories need a declared forecast origin (or an explicitly synthetic fixture), not hindsight observations. Errors provide no charge/ending state. It returns a new state without changing the opening state. A standalone non-meter board/PC/incremental-wall proxy may use time-rate energy estimates only: shared tiers, fixed/day charges, meter opening amounts and floors are rejected. It cannot become a total meter bill by renaming the scope.

`counterfactualBill` evaluates both whole-meter-import trajectories from the **same** opening state through the **same complete remaining cycle**:

```text
incremental charge = Bill(with mining) - Bill(without mining)
```

Both traces must share scope, adapter and origin. Caller-supplied trajectories include the whole non-mining baseline and proposed joint mining schedule; they are not automatically inferred from GPU watts. Different input segmentation is allowed. Fixed fees cancel only when the declared model actually leaves them unchanged; thresholds, all-units jumps and floors remain nonlinear. This is a declared full-remainder scenario, not an observed cash profit or guaranteed future bill. Unsupported persistent/out-of-cycle consequences disable evaluation. No USD conversion, income netting or automatic scheduling is implemented.

Bounds: 100,000 trace intervals/observations, 128 contiguous plan revisions, 64 bands and calendar rules per revision, 20,000 dynamic records per revision, 200,000 evaluation cuts. Capacity failures return no numeric cost. These synchronous pure computations belong in future economic/background work, not the mining/render/safety loop; no production caller is added.

## Building and verification

Default host profile needs no date/ICU/TZDB:

```text
cmake --preset tests
cmake --build --preset tests --target test_civil_time test_electricity electricity_probe
ctest --preset tests -R "^(civil_time|electricity)" --output-on-failure
```

For explicit offline named tests, build/install the separately reviewed date source with the flags above, then configure the host tests with `CANARY_ENABLE_NAMED_TZDB=ON`, `date_DIR=<date-install>/CMake` and `CANARY_TEST_TZDB_DIR=<reviewed-IANA-2026e-directory>`. No CMake fetch or download is added. Test directory selection is a development fixture setting, not a saved product configuration. Runtime code must still explicitly configure its private database; enabling the build alone does not load one.

Executed development evidence (2026-10-28):

- Windows/MSVC focused default-off regression **33/33**, including the prior product/settings/history/intent/ledger/market/helper suites plus civil time, electricity and its oracle. Optional named backend selection **5/5** passes. CMake manual-interface link probe succeeds and remote-API link probe fails as required. Early configuration failures exposed the upstream package-version mismatch and probe C++17/Release-CRT requirements; the guard now handles both without changing verification or dependency source.
- Windows and Linux/WSL/GCC each pass **157,849 dependency-free civil checks**, **157,886 checks with named data**, **149 base electricity checks**, and **166 checks with named data** (named counts include base cases). Each platform also passes **1,500 independent Python Decimal cases / 4,000 exact/display comparisons**. These are synthetic/offline host tests, not production-CUDA/game/wall-meter or supplier bill qualification.
- Linux core/test **ASan/UBSan** passes with named support both off and on; the externally built date object in the named sanitizer test is not itself instrumented. Linux used direct GCC compilation (`-std=c++17 -O1 -Wall -Wextra -Werror -pthread`), not Linux CMake qualification. No Linux CMake or production CUDA build is claimed.
- Architecture-contract tests pass. Privacy gates scan explicit staged source/notice/doc bytes and available history with the private local deny-list; no dependency archive, library, database, build log or personal configuration is staged. Windows full Python discovery, clean-machine package/trust/update authentication, production native GPU/game/long-run and all mandated release gates remain **unrun**, not implicitly passed by these selections.

Test sources include civil serial-day round trips through 2400, UTC/fixed clocks, missing/wrong-version data, named folds/gaps, 23/25h New York days, half-hour Lord Howe days, Apia's skipped date and the 2026e Winnipeg rule. Electricity cases cover exact irregular sample integration, missing/zero/gaps/clocks, fixed-fee cancellation, progressive/all-units jumps and late settlement, shared tiers, signed rates, dynamic gaps/overlaps/causality, event priority, effective revisions without resets, minimums, whole-subtotal multiplier, same-day resume/purity and unsupported/input/capacity failures. Named tests also bill both repeated hours and no nonexistent spring hour. Scope/source/basis and continued time-database mismatches cannot reuse opening ledgers; future observed telemetry cannot masquerade as a decision-time forecast.

Research specifications T01–T04, T10–T12, T17, T25 and supported portions/variants of T05, T13–T16, T23, T33–T34 are covered. **This does not mean all 34 specifications pass:** T06–T09, T18–T22, T24, T26–T32 and broader state/source-specific variants remain unimplemented. No official supplier worked example is certified by synthetic tests.

### Development dependency provenance (not production approval)

Artifacts were downloaded via official HTTPS into an isolated development directory; no implementation archive, library or timezone dataset is committed. SHA-256:

| Input | Revision | SHA-256 |
|---|---|---|
| `https://codeload.github.com/HowardHinnant/date/tar.gz/refs/tags/v3.0.5` | v3.0.5 (CMake 3.0.4) | `ef786edc203daec76475825640b3af247bd08e31fc52217e5ce8f76107b4bb05` |
| `https://data.iana.org/time-zones/releases/tzdata2026e.tar.gz` | 2026e | `b26882805f26aac59d5b222978e6580484b834ccdc98be89df2f05a6dc53a652` |
| CLDR `common/supplemental/windowsZones.xml` | `7afa5cdb6b7fe7ae65f934c6dbaa374f25b5a616` | `5f902f94106a396cb7774f0b0e1ca4dfecb686494045350c0b4935d378e3913b` |
| CLDR `LICENSE` | same revision | `65eb33419e22a297550a811b5b535f5e532f9a7e0114b516c0ea18afd53c3804` |

CLDR files came from `unicode-org/cldr` at the exact revision above. Hashes identify the observed development bytes only: independent release-signature/trusted-key authentication, maintained/security-baseline review and packaging are **not** complete. The upstream date per-file MIT notices, IANA licence and CLDR copyright/permission notice remain required in any distributing package; see `THIRD_PARTY.md`. The Unicode LDML publication restriction in CLDR's root notice is distinct from its data-file licence; no LDML publication is copied into this repository.

## Migration, rollback and remaining work

No settings, SQLite schemas, worker state, donation rules, pool traffic or installed binary changes. Disable the named option/remove the development targets to roll back; do not delete user history or modify OS timezone data. Future persistence needs its own immutable plan/state schema, semantic diff/review, recovery and explicit activation.

Remaining P-05/P-08 work includes reviewed user input/settings, source/expiry validation, further capable tariff adapters/reset ledgers, bounded telemetry ownership/persistence, qualified wall/baseline adapters, matched actual-today credit/reward forecasts, USD FX/net projection, native runtime integration and release qualification. Real credit-source gaps cannot be filled with balances, shares or this energy core. Existing miners and game/GPU controls remain untouched.
