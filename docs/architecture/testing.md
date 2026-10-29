# Test families and release gates

All fixtures are synthetic. Acceptance specifications are not passing runtime evidence. Tests must state their measurement/OS/toolchain scope; CPU stubs, WSL, fake launchers and short mining runs cannot substitute for native GPU/game/release qualification.

## T-UI

Test S entry, all keyboard contexts, focus, supported edit/toggle/select, review/save/discard, idle/no-mining command and redirected rejection. Verify D/L page switches and disabled shortcuts, active-page fallback, each Details field, Log threshold/row limit/time/follow display and continued pipe draining. Verify resize/short/no-color/plain and explicit unavailable/stale/coverage labels. Net row is absent without electricity; valuation is never called sold proceeds/current holdings.

## T-CONFIG

Test strict keys/types/ranges, old-version defaults, unsupported-new-version rejection, atomic replace, read-only/invalid/external edits, stale writers, lock/crash handling, CLI precedence and independent credential/runtime stores. A failed save cannot change effective state. Navigation cannot grant benchmark, plaintext or hardware permission.

P-03c [runtime-default evidence](../runtime-settings.md) covers native numeric editor contexts, separate UI/runtime saves, zero CLI overrides, external writers and frozen current-session values. Real PTY tests verify collection off/on only at explicit future sessions; this is not live policy or credential-store qualification.

## T-HISTORY

Test generation resets and carried counters, duplicate/reordered samples, aggregation across GPUs, donation exclusion, accepted versus pending/rejected/unknown, rolling-window exact boundaries, clock jumps and incomplete coverage. Test restart/durable recovery, compaction preserving all-time totals, truncated/corrupt writes, concurrent writer refusal, counter overflow and missing-period reporting. Never infer rewards from shares.

Executed local observation slice: [history validation](../history-validation.md). Includes process-crash/WAL recovery and PTY two-session persistence, not physical power-loss, full-disk or production-CUDA qualification.

## T-MARKET

Test correct/wrong asset and network; latest quote versus equal-time 1h average; full/duplicate/missing/incomplete minute candles; sparse time weighting; pagination; valid zero-volume candles; stale/future timestamps; provider errors/rate limits; cache/cancellation/source switch. Test token/USDT times USDT/USD, missing/depegged conversion and explicit currency labels. HTTP responses are bounded; no redirects to unauthorized schemes/hosts and no disabled TLS verification.

Executed transport-only slice: [P-06a](../public-https.md), including separate direct/process-isolated TLS fixtures, JSON/IPC limits and stalled-helper cancellation on Windows and Linux/WSL. Native-trust revocation policies differ explicitly. These tests do not qualify provider data, actual DNS stalls, live quotes, a market scheduler or production packaging/auxiliary egress.

[P-06b](../market-data.md) separately tests exact-decimal/rational arithmetic against a Python Decimal oracle, explicit Gate/Kraken fixture adapters, aligned historical FX products, full minute coverage, stale/skew/event-time rules, deterministic rate/backoff/clock/generation state, real background cache cancellation and native projection. These are synthetic/offline results, not live source/supervisor activation or actual-credit qualification.

[P-06c](../manual-market.md) adds manual decimal/identity/schema/clock/expiry/skew, storage and native editor/render tests. Actual POSIX CLI/synthetic-supervisor PTY tests cover independent saves, timestamp preservation, terminal-only live replacement/off, CLI suppression and unchanged child-start counts. Manual declarations cannot qualify a provider or create income; a small synchronous save is not hard disk-stall/stop-deadline evidence.

## T-LEDGER

Test actual credits versus balance/payout/pending/share count; worker versus wallet attribution; inclusive/exclusive time boundaries; pagination/retries/revisions/reversals and late discoveries. Test that market updates preserve token facts while changing **latest-price production valuation**. Two differently priced credit events at the same latest price must value as their total tokens times that latest price, not the historical weighted sum. Optional historical estimates remain separate. Sale status stays unknown, even after a transfer/deposit; missing/partial data never becomes zero or complete lifetime revenue.

[P-07a](../credit-ledger.md) executes the synthetic durable-core scope, including exact millisecond boundaries, identity/revision/coverage semantics, SQLite crash-before-commit recovery and rejected-database preservation. This does not establish a real pool's credit contract, collection owner/UI activation or today's forecast. Store calls remain outside the production supervisor.

[P-07b](../credit-owner.md) additionally executes native helper/IPC and bounded background ownership, backpressure, generation/stop isolation and explicit exact-batch recovery after a lost post-commit acknowledgement. It does not turn userspace stalls into genuine kernel/D-state qualification or supply real credit collection, persistent pending-journal/recovery UI or production activation.

[P-07c](../credit-recovery.md) adds durable original intent/receipt, owner recreation, checksum/binding preservation, no-auto-replay inspection, read-only current-ledger proof and failed-retry non-erasure. Separate processes exit at prepared/applied/confirmed boundaries and recover without doubling or freshening. This does not prove live-parent death with an unreaped/stuck helper, genuine power loss, source-cursor/native recovery UI or production activation.

[P-07e](../credit-cursor.md) tests atomic facts/cursor crash rollback, wrong predecessor/cycles/empty pages, strict v1/v2 and wire/journal compatibility, corruption/missing-file preservation, eight concurrent same-revision producers, stop/fault/refresh invalidation and exact recovery after prepared/unsettled/lost/bad acknowledgements. Native PTY checks committed versus pending progress and full hidden/revealed long tokens. This remains fixture-source evidence, not real-provider pagination or physical durability qualification.

## T-FORECAST

Test today's actual-so-far plus remaining eligible expected income, calendar 23/24/25-hour days, delayed/incomplete current-day credits, uncertain schedules, switching/donation/fees without double deduction, and the separate rolling-24h row. Test no-tariff row omission versus configured zero/negative rate, missing price/FX/reward/cost data, negative valid estimated net values and matching revenue/cost horizons. Labels must say estimated, not realized.

## T-TARIFF

Implement [research test specifications T01–T34](../research/international-electricity-billing.md#7-deterministic-edge-case-tests-to-implement), including progressive and all-units boundary jumps, shared-register tiers, independent resets, annual reconciliation, signed rates, dynamic coverage, DST/standard-time, event priority, effective revisions, demand averaging/ratchets/joint non-additivity, taxes, minimum floors, prepaid acquisition, export opportunity and restricted/expiring credits. These source-derived families do not certify every jurisdiction's settlement rules.

Add plan-specific official worked-example comparisons before labelling an adapter source-verified. Verify pure counterfactual evaluation from identical opening state and no look-ahead to unpublished prices/events. Reject unsupported current contract/allocation rules; do not smooth real discontinuities or use country averages silently.

[P-05a](../electricity-core.md) executes a declared supported-rule subset, exact rational arithmetic/oracle checks and explicit offline IANA day/fold/gap/TOU cases. It does not complete all 34 research specifications or qualify a supplier plan; unsupported families and dependency authentication/packaging remain listed there.

[P-05c](../tariff-declaration.md) checks native nested rule/opening edits, complete independent review/save, strict nested schema, decimal/time/capability limits, plan/opening binding and stale-writer preservation. Hand-derived combined totals and continuation exercise decoding into the existing pure core. POSIX PTY authors the example from blank fields, keeps independent UI drafts, rejects same-key edits and verifies no active-session activation plus editor SIGINT. Declared openings do not certify observed billing/reset persistence, meter/supplier identity or physical durability.

## T-ENERGY

Test elapsed-time W-to-kWh integration, irregular samples, invalid units/NaN/negative sensor power, gaps, matched baseline/subtraction, multiple GPUs/shared meters and signed cost conversion. Distinguish missing power from zero power and full-board game load from miner incremental energy. Persist and display measurement basis/coverage.

[P-08a](../electricity-core.md) executes bounded exact sample-and-hold integration with explicit scope/basis and UTC/monotonic consistency/coverage, not a real telemetry adapter, matched hardware baseline, persistent measurement store or live net-income estimate.

[P-05b/P-08b](../session-energy.md) adds native settings/store tests, explicit NVML missing-versus-zero power, generation/origin-bound streaming accumulation, effective-version/clock guards and a queue-drop continuity oracle. POSIX actual-CLI/fake-supervisor PTY verifies independent saves, frozen current/next-start activation, CLI suppression, display/collection separation and Ctrl+C through an owned controlling terminal/stop flag. Synthetic data and host tests do not qualify native NVML accuracy/stalls, whole-PC/incremental baselines, persistent energy history, bill rules or net income.

[P-05d/P-08c](../energy-history.md) tests persistent raw-power identity, exact clipped intervals and independent reset registers, missing/zero/max readings, duplicate/reordered/clock-invalid batches, explicit new sessions and Existing/CreateNew preservation. Native helpers exercise uncertain acknowledgements, stale/concurrent revision admission and stop with no late publication. Separate SQLite/process probes independently check exact energy, chain/semantic corruption and deaths before/after COMMIT; maximum channel/reading/window envelopes and ordinary-artifact seam exclusion are included. These are bounded host observations, not native persistence UI/telemetry activation, indefinite retention, monetary bill continuation, authentic sensors or physical-disk/kernel qualification.

[P-05e/P-08d](../energy-recovery.md) adds separate-parent reconstruction at Prepared, committed-without-receipt and confirmed-without-reply boundaries. Verify immutable structured originals, exact prior/result chain proofs, missing/corrupt/rechecksummed/wrong-binding journal preservation, both nonblocking leases, failed/mutated/stale/concurrent retry, no old-session continuation, full-size IPC and no late publication after stop. Reopening may checkpoint SQLite WAL without changing facts; compare logical events and exact journal content rather than assuming all database bytes are stationary after abrupt exit. Ordinary artifacts exclude fixture exit/stall seams. Live-parent death with an outstanding helper, physical-disk/power/kernel behavior and production collection remain separate qualification.

[P-07d](../credit-review.md) adds native private-default/status/original-input/acknowledgement tests and real helper tests for lost-reply retained input, no-auto-apply inspection, stale final-click refusal under an owner mutex and exact one-shot retry with unchanged observation times. POSIX PTY uses the actual settings plus injected fixture owner for reveal/cancel/dirty-draft preservation/duplicate Enter/confirmation/SIGINT. These do not qualify source/cursor/import/export/production binding or genuine parent-death/kernel/power-loss recovery.

## T-PROFILES

Test zero-ms throttle semantics and safe complete-work-unit boundaries, warm-up/exclusion, repeated windows, bounded time/energy costs, cancellation and faults. Test profile fingerprints, import hints versus measured eligibility, drift/software/environment invalidation and no full-speed sweep during play. No arbitrary device whitelist or kernel-math expansion as a side effect.

P-03b [host-wait evidence](../work-wait.md) covers deterministic wait clocks, CLI admission and actual Linux `run.cpp` early-refusal wrappers. It does not qualify a production GPU's wait/energy/game behavior or implement measured profile selection.

## T-OPTIMIZER

Test best eligible estimated net per elapsed time, no-data/nonpositive pause, all configured timers independently, source/curve expiry, hysteresis/cooldowns, switch/test costs and stale asynchronous results. Test current prices rather than historical per-credit valuations, joint-meter interactions and separate normal/game candidates. Heuristic margins are not statistical confidence bounds.

## T-GAME

Test off/pause/coexist, launcher/game/manual triggers, debounce/detection failure, no permission/no FPS/no valid baseline safe fallback, actual matched scene metrics, experience limits/VRAM/thermal headroom and bounded authorized testing. Test qualified auto-entry/exit restoration, economic pause/recovery and user stop not reviving. No injection/anti-cheat bypass/focus stealing/game killing; generic launcher tests do not count as gameplay evidence.

## T-TLS

Use synthetic local CA fixtures for valid chain/name, wrong host/issuer, expiration, absent trust and custom CA. Test SNI, handshake/cancellation deadlines, partial TLS records/writes/buffered plaintext, read/write readiness, EOF/reconnect and no application bytes before authentication. Test strict SSL, per-pool fallback off/on, certificate errors never downgrading, explicit endpoints/no scanning, donor sessions and ambiguous-submit non-replay. A consented network-error fallback is still not downgrade-resistant.

## T-INTENT

Test priority of user stop, latched fault, thermal/telemetry limits, game constraints, economics and ordinary work. Q/Ctrl+C/close invalidate pending results/retries; no menu/save/refresh/login action starts mining. Test outer advanced-launcher/service restart boundaries as well as supervisor flags. Test bounded worker drains, crash budgets/cooldowns, helper ownership/restore without clobbering external changes, PID generation, no uncontrolled driver reset/reboot and no privilege escalation in network/UI code.

Executed P-03a [intent/launcher evidence](../session-intent.md) covers explicit-start tokens, actual fake-worker supervisor exits and copied native launchers. Game/economic arbitration is still a pure contract, not runtime telemetry or game qualification.

## T-I18N

Test all declared locales/key coverage, placeholders/plurals, review/fallback status, UTF-8/grapheme/combining/wide/RTL snapshots, numeric/date/currency formatting and invariant flags/JSON/protocol/event codes. Changing language must not change tariff, timezone, currency, device or prices.

## T-PRIVACY

Scan staged/committed files and locally available history with a private untracked deny-list. Test redacted exports/logs, default local-only financial/game records, credential-vault references, bounded retention/delete and opt-in provider disclosures. Inspect build archives, debug symbols, screenshots, branch/commit metadata and action logs. Never upload a wallet, personal tariff, host/device identity, private path or raw runtime file as evidence. Preserve upstream public attribution/licences.

## T-RELEASE

Run Windows/Linux host suites and actual native-platform integration where required. Before upstream production release, run all mandated pinned-toolchain/verifier/secret/protocol/SASS gates from CONTRIBUTING.md, dependency/licence/package tests, configuration migration/rollback, optional public-endpoint smoke tests and authorized long-duration GPU/game qualification. Record exact redacted commands, failures and unrun gates; no fabricated evidence or claims that a private deployment proves these gates.
