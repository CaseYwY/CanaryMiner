# Native flat-energy defaults and session GPU-board estimates (P-05b / P-08b)

**Default off; development integration, not an actual-profit or utility-bill feature.** This adds native reviewed electricity defaults and an optional in-memory background session energy owner to the ordinary supervised miner. It does not start an actual-credit collector, supply today's forecast/net income, select an economic profile, tune hardware or replace an installed miner. `FinancialView.enabled` remains false.

Requirements: R-UI-007/008, R-TAR-001/006, R-ENG-001, R-SAFE-001/002 and R-PRIV-002, all still partial for their full scope. Related tests: T-UI/T-CONFIG/T-ENERGY/T-TARIFF/T-INTENT/T-PRIVACY. [P-05a/P-08a](electricity-core.md) remains the more capable **pure** tariff/time arithmetic; this initial native page does not silently simplify its unsupported combinations.

## Native controls and explicit activation

Open **S → Electricity → Session board energy / declared flat energy-only rate**, or `canary-miner settings [--config FILE]` without mining.

- **Collect session board energy:** off by default. This opts in to an in-memory session model, not durable history or mining permission.
- **Show energy section:** independent from collection. Hiding presentation must not stop an explicitly enabled collector. Both are frozen for the current session.
- **Record selection:** independently 500–60000 ms, initially 1000. Selects fresh observations from the supervisor's **existing** telemetry calls. It does not resample a cached value, increase GPU polling, control display refresh or change the worker's work-wait interval. Existing supervisor cadence/coalescing means requested selection is not a sampling SLA.
- **Maximum hold gap:** selection interval through 60000 ms, initially 5000. Longer/missing intervals are unobserved, not zero or nominal-time fill.
- **Use declared flat energy rate / acknowledge exclusions:** both off initially. A rate requires an explicit acknowledgement that the estimate excludes tiers, TOU, dynamic rates, fees, taxes and shared-meter bill consequences. This is an intentionally selected **energy-only proxy**, not a claim that a real utility tariff is flat.
- **Country/jurisdiction, region/territory, supplier, product/plan, effective version, currency and rate:** independent local inputs, no personal defaults. Nothing is inferred from IP, locale, GPU, OS zone or currency symbol. Labels are bounded ASCII declarations, not account/credential fields or verified geographic/supplier IDs. Three-uppercase-letter currency is an identifier constraint, not ISO/legal-tender/FX qualification. USDT is not silently USD.
- **Rate:** signed exact decimal text (38 digits / 18 decimal places maximum). Explicit zero is valid; empty is missing. Negative rate can yield a negative **reference energy cost**, not guaranteed utility cash credits or profit. No exponent, binary-float storage, whitespace, nonfinite value or intermediate rounding.
- **Reporting clock:** explicit UTC or a whole-minute **fixed STANDARD** offset within +/-18 hours. This constant-rate session proxy does not perform daily billing or DST transitions. Named IANA/reporting-day settings and the qualified private data package remain a later integration; there is no OS fallback or country-based zone guess.
- **Effective start/end:** explicit UTC Unix seconds, with an exclusive end. N on the start field explicitly stamps now; Save never silently renews these boundaries. Confirmation records the local settings action, not supplier authentication or market freshness.

The cooperative store refuses identity/rate/clock/effective-range edits under the same nonempty stored version key: choose a new key in Review/Edit. Collection/display/cadence toggles do not change the tariff declaration. This is a comparison with the loaded local target, **not** an authenticated global version registry or a historical billing ledger; external/moved historical versions still need their own future migration/audit design. Review shows the frozen current tariff separately from configured next-session values.

Tab/arrows select a field. Enter/Space changes a supported value; Enter in an editor applies only its draft. Esc cancels that edit. Q/D/L/S in an editor are text, never global actions; Ctrl+C still stops. Overlong/non-ASCII input is rejected rather than silently truncating identities. V/F2 reviews before/after, model/basis, clock/effective range, CLI precedence and save target; Enter/F2 explicitly saves. Dirty leave offers review, discard or continued editing. Failure retains drafts/current values.

**Every control on this page activates only at the next explicit parent session**, never by save, telemetry refresh, price update or child retry. Saving does not clear the current model, reprice it, rewrite mining config, delete history or commit UI/runtime/manual-market drafts. There is no fictitious multi-file transaction.

**`--no-energy`** suppresses this session's optional energy recording/display, even after saving enabled future defaults. It preserves the saved file and does not stop ordinary safety/statistics telemetry or comprehensively anonymize existing logs/API. Idle settings accepts it so the review can show that precedence. Plain summaries do not print tariff/energy values; explicitly enabled session collection can still run without a dashboard.

## Measurement contract and honest scope

The new `GpuTelemetry::powerValid` is independent of device availability: NVML `nvmlDeviceGetPowerUsage` success sets it, including for a genuine zero. Missing function/failed read leaves it false; a default numeric zero cannot create an energy observation. The native product stats/dashboard now distinguish that missing reading from zero. This does not qualify other legacy telemetry backends or add hardware support beyond the existing mining scope.

The collector uses only the existing successful NVML **integer milliwatt** reading, never the rendered watts/double JSON value, share count, fee balance or another process's attributed income. No new sensor/driver/network call is made by the owner or a render callback. Driver-reported power may itself be averaged/uncalibrated; this model is not a continuous or wall-meter measurement.

Typed evidence fixes the basis to **GPU board**, with explicit observed-telemetry versus synthetic origin and adapter, ordinal selected-board scope and generation. Synthetic tests stay conspicuously labelled; a data generation is not mining authorization. Different scopes, source families or whole-system/incremental/whole-meter evidence cannot be asserted by renaming a tariff field.

For each selected board independently:

1. Hold the previous available power until the next observation only when monotonic time increases and the gap is within the configured bound. A missing current reading ends the previous hold; it cannot start the next one. Valid zero still contributes covered time.
2. Never backfill startup, excessive gaps, worker restart intervals or a final unobserved tail. Missing boards do not become zero-valued complete aggregate data.
3. Keep the exact W·ms numerator (raw mW × elapsed ms × 0.001), divided by **3,600,000,000** for kWh. There is no per-observation rounding. Six-place half-even display marks nonzero sub-resolution values rather than inventing zero.
4. Aggregate known energy only across the frozen selected board channels. Coverage is **GPU-device time**: covered GPU-ms / (selected board count × observed elapsed horizon), not a claim of continuous sensor accuracy or common complete coverage at every wall instant.
5. Include total board power while observed, including idle/display/game and developer-donation power. It is not miner-only incremental consumption, whole-PC draw, wall-profit proof or a reason to reduce/relabel donation.

The owner anchors an explicitly declared **UTC/monotonic mapping** at session start. Model UTC is anchor UTC plus monotonic elapsed time; the supplied wall sample must stay within a visible **2000-ms guard**, without rollback. Clock regression, duplicate same-board monotonic time, out-of-range mapping or excess drift latches the owner; known prior values remain labelled and a new explicit session is required. This is not silent timestamp repair or millisecond-accurate provider time. Endpoint cost eligibility conservatively requires the whole held interval, expanded by that guard, to remain inside the declared effective version.

A future settings-confirmation instant relative to the new session's UTC anchor refuses tariff-backed collection rather than pretending the clock is valid. Effective start may legitimately be in the future; those intervals simply have no eligible priced coverage before that declaration applies.

## Energy-only cost, not a shared bill or net forecast

If an enabled declaration covers a held interval, its exact W·ms numerator is multiplied by the signed currency/kWh rate, retaining the same denominator. Unpriced intervals remain unpriced; a known subtotal carries its own priced coverage and is not a complete bill. An expired/missing version or no covered power yields unavailable, never a fabricated zero. Changing currency does not invoke an FX source.

The terminal section **SESSION GPU-BOARD ENERGY / DECLARED ENERGY-ONLY COST** shows basis/model, source, observed horizon, device-time coverage, gaps/dropped observations, optional declaration/version/effective range/confirmation and priced coverage. A late/current tail remains explicitly unavailable; no result becomes fresh merely because a frame renders.

No shared household allowance, meter opening balance, all-units jump, fixed fee, minimum bill, tax ordering or future schedule is inferred. The pure core's supported combined rules and full-meter counterfactuals remain separate and unused here. No actual-income/forecast/net row is enabled, even with a configured tariff. Matching qualified revenue/FX/time/energy horizons remain prerequisites to estimated net USD.

## Bounded ownership, storage and rollback

`SessionEnergySink` accepts generation-bound fixed-size observations through a **128-job queue**, for **1–64 channels**, and publishes immutable snapshots. Decimal accumulation/formatting is background work; rendering does no tariff calculation or I/O. A full queue returns `Full` and advances a continuity epoch: the next accepted sample cannot bridge over an unobserved dropped value, even if the apparent gap is short. Origin/time/capacity faults never auto-retry or affect worker authorization.

`requestStop()` revokes new admission/publication and discards queued observations; `stop()` joins outside the worker drain/safety path. No detached task, shutdown flush, restart token, OS reset or extra hardware permission is created. Queue/wire bounds are not promises of total heap limits, scheduler deadlines or behavior during genuine kernel/OOM failures.

The displayed queue drop/discard counter is not a complete loss audit: an in-flight result revoked by stop and the unobserved tail are not silently counted as saved or covered. Coverage and through instants, not a zero drop count, define the known model prefix.

`electricity-preferences.json` schema v1 is an independent small local file ([disabled example](../config/electricity-preferences.example.json), [schema](../config/electricity-preferences.schema.json)). Strict keys/types/ranges/model/basis and stale-writer/OS-lease/atomic-replacement checks preserve unknown/newer/corrupt/nonregular files. Opening missing settings creates no preferences. New POSIX files are 0600; Windows inherits the protected data parent ACL. The existing **synchronous small-preference save path** remains a disk-stall/physical-power-loss/hostile-directory release gate; this is not the helper-isolated credit journal.

Energy totals are **session memory only**, not 24h/7d/30d/all-time history, a persistent billing/reset ledger or a recovery source. Process exit discards the model without claiming unobserved tails were saved. Existing accepted/credit stores and UI/runtime/market schemas do not migrate. Rollback ignores the new electricity file; preserve historical stores and use the explicit off/CLI override where desired, not deletion as repair. Private rates, labels, raw readings and configuration remain local and ignored by Git.

The pre-existing native NVML calls in the supervisor can still block in the driver; this batch does not qualify driver-call isolation, actual hardware sensor semantics, long-run accuracy or a hard user-stop deadline under OS/driver failure. No installed miner, device settings, kernel/proof mathematics or pool protocol bytes are deliberately changed; full native CUDA/SASS/packaging qualification remains required.

## Validation and remaining work

Host tests use only synthetic values and inert workers. They cover missing versus zero power, exact/fractional/maximum raw readings, signed/zero rates, scope/origin/generation, version/clock guards, excessive gaps, no extrapolation, queue-drop continuity against an independent admission oracle, immutable snapshots, cancel/no-late-publication, separate saves, stale writers, strict schema, editor context/overflow and no-color focus at 60/80/120 columns and 12/24 rows.

The real POSIX PTY covers idle save with a separate unsaved UI draft, an active session retaining collection after saving off, a next session staying off after saving on, CLI suppression, and a hidden display retaining collection. Missing synthetic-worker sensors remain N/A, never injected real measurements. The test owns a controlling terminal and the fake supervisor installs the ordinary stop-signal flag, so Ctrl+C in the editor exercises SIGINT rather than relying on a swallowed control byte. An initial harness omitted that controlling-terminal/signal setup; Q stopped normally and draining output did not fix it. The corrected regression tests the real signal path, not a claimed production energy-thread hang.

Executed evidence (2026-10-28):

- Windows/MSVC focused CTest **43/43**, including existing donation, settings, accepted history, shutdown/intent, credits/process/crash recovery, market/HTTPS and exact energy/oracle tests. The new session energy/settings program passes **233 checks**; five consecutive standalone repetitions pass. Five existing credit-owner repetitions also pass.
- Linux/WSL/GCC: **233 session energy/settings checks** (also five consecutive standalone runs), **24 new native PTY checks**, and rebuilt existing settings/runtime/manual-price PTY suites pass. Own session owner/decimal/test code passes **ASan+UBSan with 233 checks**; no native driver/sensor or whole-system measurement is certified by those synthetic tests. Builds use direct GCC commands, not Linux CMake qualification.
- Linux Python discovery: **122 tests, OK with seven fixture-dependent skips**; skipped executable fixtures are not assumed passes. Explicit operator-help/exact-profile CLI tests pass **12+1**; schema/build-recipe/import/architecture programs pass **7/2/1/4** tests. Native Windows full Python discovery/GNU-toolchain/default-encoding limitations remain unqualified.
- The legacy compile-only native recipe includes the new **host-only** energy object without changing existing CUDA flags/seeds. Two fake-compiler recipe tests verify construction/link inputs only, not real nvcc/SASS/production qualification. No dependency is downloaded, bundled or installed by this slice.

Source/index/history privacy checks use the untracked local deny-list; runtime data, private configuration, binaries, build/debug artifacts and raw measurements are not contribution evidence. These are host/fixture results, not a new release or real GPU-game qualification.

```text
cmake --preset tests
cmake --build --preset tests --target test_session_energy test_product_supervisor test_pearl_cuda_cli_host
ctest --preset tests -R "^(session_energy|session_energy_terminal|config_schemas|native_build_recipe)$" --output-on-failure
```

[P-05c](tariff-declaration.md) subsequently adds separate native combined-rule and declared-opening settings, without feeding this session owner. Still unfinished: observed persistent billing ledgers and independent reset clocks, named reporting-zone packaging, persistent energy history, qualified driver/whole-PC/incremental adapters and baselines, real-source actual-today/reward forecasts and net FX integration, economic curves/policy/game/TLS/i18n and full release qualification. The whole planning-book objective remains active.

[P-05d/P-08c](energy-history.md) subsequently implements a separate bounded raw-power history/reset-window core and isolated owner. It does not change this session collector's memory-only activation or convert its generation/ordinal label into persistent identity. Native persistence consent/binding, long-run retention, durable pending input and the producer bridge remain integration work; monetary bill state and the other forecast/release gates above remain unfinished.
