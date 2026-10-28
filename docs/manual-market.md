# Native manual/offline reference inputs (P-06c)

**Default off; user declarations, not live quotes.** The ordinary supervised terminal can display explicitly entered Pearl mainnet native PRL/USDT and an independently entered USD-per-USDT conversion. No public provider, credit collector, forecast, net-income row, optimizer or GPU control is enabled. P-06 remains `PARTIAL`.

This slice implements the offline-input part of R-MKT-003 and presentation/configuration portions of R-MKT-001, R-UI-007/008. Relevant test families are T-UI, T-CONFIG, T-MARKET, T-INTENT and T-PRIVACY. It does not qualify a source under the [public-feed contract](market-data.md).

## Native controls

Open **S → Market and timing → Manual/offline price references**, or run `canary-miner settings [--config FILE]` without mining. The same subpage is available without a wallet or GPU; noninteractive/redirected settings are rejected.

- **Display enabled:** explicit off/on. Enabling requires a positive PRL/USDT value and its reference UTC instant. Merely opening the subpage writes nothing.
- **PRL price:** USDT per PRL, never USD, an ERC-20 alias or a different asset with a similar ticker. Values are base-10 strings, at most 38 digits and 18 decimal places. Leading/trailing zeros canonicalize without rounding; zero, signs, exponent notation, spaces, symbols and numeric JSON prices are rejected.
- **PRL reference UTC:** integer Unix seconds, not local calendar time or milliseconds. Enter edits it; **N** on the timestamp field explicitly stamps the current UTC time. Use N only for an input genuinely referenced to now. Editing the price or saving does not silently stamp it.
- **FX enabled / FX price / FX reference UTC:** independent declaration of **USD per USDT**, with its own timestamp. No fixed peg or inverse conversion is assumed. FX can remain off while PRL/USDT is shown.
- **Validity seconds:** 60–86400, initially 3600. This is a maximum reference age, **not** a polling interval, a promise of accuracy, or an hour of price observations.
- **Maximum FX skew seconds:** 0–3600, initially 30. A USD product needs both valid references and timestamp separation at or below this limit. Increasing a limit is a user assumption, not a source qualification.

Tab/arrows select a field. Enter/Space edits or toggles; Enter applies only the draft. Esc cancels a value edit. Backspace works; Q/D/L/S typed in an editor are text, not global actions. Ctrl+C still stops. Invalid input remains visible with a wrapped explanation. **V/F2 → review → Enter/F2** is the only save action. Dirty leave offers review, discard or continued editing; Q never implicitly saves.

Review names the one save target, before/after values and activation scope. UI and runtime-default drafts remain separate. Successful save in an already-running supervised dashboard replaces only this terminal's references; it does not restart workers, change their work-wait defaults, alter history, change donation or grant mining/network permission. Idle settings affect the next explicit dashboard session only.

**`--no-market`** suppresses this session's offline reference display, including after a later Save; it neither changes the persisted enabled flag nor forbids editing for a future session. It is also accepted by the idle settings command so its review can show that override. `--no-dashboard` and plain summaries do not print prices. This flag is not a future network-source consent mechanism.

## Honest display and clocks

The separate **MANUAL/OFFLINE PRICE REFERENCES** section says **USER DECLARATION — NOT LIVE**. It displays asset/units, each supplied UTC instant and age, confirmation UTC, validity and FX skew. Confirmation records the settings action's OS-clock time, **not** a trade event, provider observation, authenticated timestamp or append-only audit trail. A hand-edited file remains the user's own declaration.

- Display uses exact decimal multiplication followed only by explicit eight-place half-even presentation rounding. A positive value smaller than that display resolution is `<0.00000001`, not zero.
- Missing/stale FX leaves USDT intact and USD unavailable. A stale token input is unavailable; it is never refreshed by re-saving unchanged reference instants. Validity/skew equality is accepted.
- **1h equal-time mean is N/A:** a point declaration provides no complete minute series, interval coverage or exchange trade-event freshness.
- Activation validates the declaration and current UTC. During the session, age uses the activation age plus monotonic elapsed time. An observed rollback or UTC/monotonic drift over five seconds latches the projector unavailable; clocks catching up cannot revive it. A new explicit review/save or parent session creates a fresh projector, but still uses the old reference times unless the user changes them.
- The projector is single-terminal-owner state, not a concurrently callable financial-data API. It produces a read-only `PublicMarketView`, never a qualified `MarketObservation` or credit/policy input. It performs no I/O or decimal calculations per frame.

There are no actual-income, today-forecast, sold-proceeds, holdings or estimated-net rows as a side effect. [FinancialView](display-settings.md) remains disabled in the production supervisor. Actual income still requires separately qualified attributable credits.

## Storage, migration and rollback

`manual-market-preferences.json` schema v1 is a small independent file in the configured state directory. [The example](../config/manual-market-preferences.example.json) contains no enabled reference or price; [the schema](../config/manual-market-preferences.schema.json) describes its shape. The native validator additionally checks paired value/time fields, positive fixed-point capacity and confirmation ordering for enabled values. Asset/currency identities are fixed and cannot be renamed via JSON.

Missing file means disabled defaults and is not written on load. Unknown versions/keys, duplicate keys, bad types, malformed/oversized data and nonregular targets are refused without clearing/rebuilding the file. Load does not auto-apply a later external edit to a running display: reopen the subpage to review it, then explicitly save. Configured and current values are distinguished. A cooperating writer holds a separate OS-owned `.lock` lease and compares the original bytes before atomic replacement; a stale writer must reopen. Symlink targets are refused, but this is not a hostile-local-user sandbox or race-proof credential store.

Save creates an exclusive temporary file, flushes it and replaces the target; failure does not activate the draft. POSIX new files use mode 0600; Windows uses the state directory's ACL policy. **This small preference operation follows the existing synchronous settings-save path.** Genuine disk/kernel stalls, physical power-loss durability (including directory metadata), hostile filesystem races and bounded stop during a stuck preference save are unqualified release gates. Neither bounded JSON size nor a passing fake-worker test proves a hard OS-I/O deadline.

There is no UI/runtime/credential/history/credit schema migration, automatic provider fetch or pending import queue. Rollback to the prior build ignores this independent file. Use the explicit off toggle or `--no-market` before rollback if desired; do not delete credit/history or mining configuration. Runtime preference filenames are ignored by Git and must never be committed or uploaded as evidence.

## Validation and limits

Host tests use invented values and inert workers only. The suites cover decimal/JSON boundaries, no-peg conversion, skew/expiry equality, timestamp identity, clock faults, independent saves and stale writers, lock/file failures, keyboard contexts, no-color/resized frames and CLI override. POSIX PTY exercises the actual idle CLI and fake-worker supervisor: save/review, no GPU setup, live display replacement/off, preserved reference times, separate unsaved UI draft and unchanged child-start count.

Executed evidence:

- Windows/MSVC: **688 manual-input checks, zero failures**; the final focused CTest regression passed **40/40**, including existing credits/process recovery, prices, energy, donation, shutdown, history, settings, intent and launcher cases. Five repeated credit-owner runs also passed. The narrower manual suite has two fewer filesystem checks than POSIX because unprivileged Windows symlink creation is not assumed.
- Linux/WSL/GCC: **690 manual-input checks, zero failures**, including symlink-target preservation; the same 690 pass ASan+UBSan. Existing settings and runtime-settings PTY suites pass; the new manual-market PTY passes **20 checks**. State/render cases exercise 60/80/120 columns and 12/24 rows without color, not sub-60 or multilingual qualification.
- Linux full Python discovery: **121 tests, OK with 7 skips**. Fixture-dependent launcher/CLI skips require separate executable-based tests, not assumed success. Explicit operator-help/exact-profile CLI tests passed **12+1**; schema/build-recipe/import/architecture programs passed **6/2/1/4** tests. This is not native Windows full Python discovery, whose previously recorded GNU-toolchain/default-encoding limitations were not requalified.
- Source/index/history privacy scans passed with the local untracked deny-list; staged binary/runtime artifacts were absent. No account, pool, hardware mining or installed binary was used as evidence.

During validation, initial PTY waits incorrectly treated a shared editor heading as browse readiness and assumed the parent group reset; the test now waits for the actual state and retains group context. A new error-message assertion initially expected one unwrapped line and was corrected to verify wrapped text. The final runs above include those corrections; no failing attempt is counted as a pass.

Reproduce the focused suite with the CPU-only preset (use executable suffixes on Windows):

```sh
cmake --preset tests -B build/manual-market-check
cmake --build build/manual-market-check --target test_manual_market test_product_supervisor test_pearl_cuda_cli_host
ctest --test-dir build/manual-market-check -R '^(manual_market|manual_market_terminal|config_schemas|native_build_recipe)$' --output-on-failure
```

Windows native tests and Linux/WSL host/PTY/sanitizer results are development evidence, not a new production CUDA or native Linux gaming qualification. Real source/candle/trust-egress/terms, persistent candle archive, reward/credit source, today's forecast, energy/net integration, all multilingual terminal support and full pinned release gates remain unfinished. No installed miner or production binary is replaced.

The legacy compile-only `tools/build_natives.py` recipe now includes the two host-only projector/arithmetic objects without changing existing CUDA compile flags/seeds; `--only` still compiles just the kernel unit. Its fake-compiler recipe tests prove command construction/link inputs, **not** a successful real nvcc build, SASS identity or release qualification.
