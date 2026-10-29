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

[P-08j](../income-forecast.md) exercises these pure composition boundaries using a synthetic ledger and explicit fixture earning/work contract: midnight/end-of-day inclusivity, exact rolling windows, complete/partial/zero/negative facts, weighted reward integral, coherent snapshot/profile invalidation and full future coverage. Verify exact numerator/denominator and half-even display with independent Fraction/Decimal oracles, same-day signed costs/currency FX, hidden unconfigured net row, 23h/25h named-zone energy, finite arithmetic/interval budgets and private-default exact terminal lines. Native preview uses six modes at 60/80/120 columns plus the established below-60 clamp. These are not real reward/work/source, accrued bill or runtime clock-owner qualifications.

[P-08k](../forecast-owner.md) exercises existing-only readonly helpers, unchanged source originals, missing/orphan/Prepared preservation, all 64 calibration periods and source changes between query chunks. Verify independent quote/tariff-FX updates, frozen overrides, in-flight invalidation, total read timeout/no retries, source rollback refusal, both-clock fault latches, stopped snapshot freeze and no late publication. Synthetic supervisor/PTY and true owned-parent termination cover native cancellation and parent-bound reader exit; public stats/artifact checks exclude private forecast state and fault seams. Kernel/physical durability, real provider/work and deployment qualification remain separate.

## T-TARIFF

Implement [research test specifications T01–T34](../research/international-electricity-billing.md#7-deterministic-edge-case-tests-to-implement), including progressive and all-units boundary jumps, shared-register tiers, independent resets, annual reconciliation, signed rates, dynamic coverage, DST/standard-time, event priority, effective revisions, demand averaging/ratchets/joint non-additivity, taxes, minimum floors, prepaid acquisition, export opportunity and restricted/expiring credits. These source-derived families do not certify every jurisdiction's settlement rules.

Add plan-specific official worked-example comparisons before labelling an adapter source-verified. Verify pure counterfactual evaluation from identical opening state and no look-ahead to unpublished prices/events. Reject unsupported current contract/allocation rules; do not smooth real discontinuities or use country averages silently.

[P-05a](../electricity-core.md) executes a declared supported-rule subset, exact rational arithmetic/oracle checks and explicit offline IANA day/fold/gap/TOU cases. It does not complete all 34 research specifications or qualify a supplier plan; unsupported families and dependency authentication/packaging remain listed there.

[P-05c](../tariff-declaration.md) checks native nested rule/opening edits, complete independent review/save, strict nested schema, decimal/time/capability limits, plan/opening binding and stale-writer preservation. Hand-derived combined totals and continuation exercise decoding into the existing pure core. POSIX PTY authors the example from blank fields, keeps independent UI drafts, rejects same-key edits and verifies no active-session activation plus editor SIGINT. Declared openings do not certify observed billing/reset persistence, meter/supplier identity or physical durability.

[P-05g](../billing-ledger.md) checks exact prefix/full-cycle equivalence, provisional all-units repricing, end-only floors, signed rates, no duplicated fees and named-clock 23h/25h restart settlement. An independent Fraction oracle compares exact numerator/delta/energy and half-even display. Separate SQLite/process tests cover empty-versus-zero, immutable binding, replay/conflict/gap refusal, read-only preservation, orphan companions, page-cap rollback and pre-COMMIT death; rehashed fabricated state must fail arithmetic replay. The page-cap fixture does not establish physical disk failure or source/helper/runtime qualification.

[P-05j](../billing-source.md) checks source-anchored exact receipts across raw/compacted histories, same-head slices, restart/replay, changed clocks/bindings, uncertain/missing sources and greater-sequence forks. Retained/expired ancestry and combined maximum envelopes exercise the actual query helper. Independent SQLite mutation rehashes invalid source anchors/receipts and requires preserved rejection; pre-monetary-COMMIT death keeps the old source-progress/money transaction atomic. These remain synthetic, without pending monetary recovery or native activation.

[P-05k](../billing-pending.md) checks durable monetary originals at flushed-temp, Prepared, pre/post-COMMIT and settlement boundaries, including failed-settlement restart. Independent processes retry exact receipts after their synthetic source is removed; stale/changed original, failed retry and divergent/absent/corrupt pairs preserve bytes. Maximum source envelopes and a >1-MiB framed binding/chunk round-trip without widening shared parsers. Readonly methods refuse writes and ordinary artifacts exclude crash seams. Physical storage and bounded native transport remain separate qualification.

[P-05l](../billing-query.md) checks full-original paging, changed-view discard, malformed headers/frames, generation/ticket/hash/offset consistency, wrong helpers and whole-transaction Busy/cancel/deadline. Parent-death probes own their processes and preserve originals; Prepared and committed-but-unconfirmed views remain readonly with the energy source offline. Large canonical binding and maximum source receipts exercise the new decoder/helper, without production/source or write-transport qualification.

## T-ENERGY

Test elapsed-time W-to-kWh integration, irregular samples, invalid units/NaN/negative sensor power, gaps, matched baseline/subtraction, multiple GPUs/shared meters and signed cost conversion. Distinguish missing power from zero power and full-board game load from miner incremental energy. Persist and display measurement basis/coverage.

[P-08a](../electricity-core.md) executes bounded exact sample-and-hold integration with explicit scope/basis and UTC/monotonic consistency/coverage, not a real telemetry adapter, matched hardware baseline, persistent measurement store or live net-income estimate.

[P-05b/P-08b](../session-energy.md) adds native settings/store tests, explicit NVML missing-versus-zero power, generation/origin-bound streaming accumulation, effective-version/clock guards and a queue-drop continuity oracle. POSIX actual-CLI/fake-supervisor PTY verifies independent saves, frozen current/next-start activation, CLI suppression, display/collection separation and Ctrl+C through an owned controlling terminal/stop flag. Synthetic data and host tests do not qualify native NVML accuracy/stalls, whole-PC/incremental baselines, persistent energy history, bill rules or net income.

[P-05d/P-08c](../energy-history.md) tests persistent raw-power identity, exact clipped intervals and independent reset registers, missing/zero/max readings, duplicate/reordered/clock-invalid batches, explicit new sessions and Existing/CreateNew preservation. Native helpers exercise uncertain acknowledgements, stale/concurrent revision admission and stop with no late publication. Separate SQLite/process probes independently check exact energy, chain/semantic corruption and deaths before/after COMMIT; maximum channel/reading/window envelopes and ordinary-artifact seam exclusion are included. These are bounded host observations, not native persistence UI/telemetry activation, indefinite retention, monetary bill continuation, authentic sensors or physical-disk/kernel qualification.

[P-05e/P-08d](../energy-recovery.md) adds separate-parent reconstruction at Prepared, committed-without-receipt and confirmed-without-reply boundaries. Verify immutable structured originals, exact prior/result chain proofs, missing/corrupt/rechecksummed/wrong-binding journal preservation, both nonblocking leases, failed/mutated/stale/concurrent retry, no old-session continuation, full-size IPC and no late publication after stop. Reopening may checkpoint SQLite WAL without changing facts; compare logical events and exact journal content rather than assuming all database bytes are stationary after abrupt exit. Ordinary artifacts exclude fixture exit/stall seams. Live-parent death with an outstanding helper, physical-disk/power/kernel behavior and production collection remain separate qualification.

[P-05f/P-08e](../energy-compaction.md) tests opt-in v2 exact aggregate checkpoints against independent interval sums at arbitrary millisecond edges, randomized missing/zero/power/session/continuity changes, active-tail continuation and reset projections. Exercise raw expiry versus retained replay, policy/schema mismatch, capacity preservation, 64-day compressible synthetic reuse and full 32768-segment parsing/reopen. Independent probes kill before/after checkpoint+expiry COMMIT and rechecksum malformed snapshot states. The latest original must remain valid for Prepared/Confirmed reconstruction across compaction. These do not qualify actual sampling-cadence retention duration or raw-detail archival.

[P-05h](../energy-trace.md) tests a separate READONLY open mode and coherent complete-power snapshots, with independent frame-oracle expectations across raw and compacted histories. Verify asynchronous channels, exact clipping/sums, partial-versus-zero, immutable returned values, 64 maximum-power channels, 512/513 output bounds, preserved provenance, chronological tariff costs and old-IPC mode rejection. Independent SQLite mutations/byte checks cover v1/v2 audit rejection and orphan preservation. These do not establish journal-aware source permission, monetary commit binding, a bounded query owner or physical sensing.

[P-05i](../energy-trace-helper.md) tests QueryOnly journal rejection and parent-bound exact-query IPC across all recovery states, including committed-but-unconfirmed input. Round-trip 64 channels/512 intervals with a maximum original envelope; reject missing/corrupt stores, write/create modes, malformed or inconsistent traces, cancellation, concurrent calls and wrong helpers. An owned synthetic parent-death probe checks child termination, source preservation and fresh readback. This does not certify physical kernel/disk stalls, global helper quarantine or monetary/runtime activation.

[P-08f](../energy-capture.md) verifies selected-set mapping under device reorder, selection-change refusal, explicit authorization, fresh parent monotonic anchors, maximum channel/buffer envelopes and exact integrals across overflow/decimated missing reads/external gaps. Verify blocked startup inspection, immutable Prepared review, concurrent revision-bound retry, old-session refusal, retained retry receipts, clock latch, refused UI-admission fault publication and stalled-helper stop without late results. Native renderer, controlling-terminal reveal/cancel/retry/SIGINT and synthetic supervisor factory/CLI/stop checks remain CPU fixtures, not production telemetry or durable identity-store qualification.

[P-08g](../energy-authorization.md) tests default-off canonical consent CAS, immutable enrollment/retention, system-random private provisioning, 64-device envelopes and explicit parent handoff. Separate processes exit after identity reservation, empty-store creation and fresh-session persistence; lost replies never issue plans or cause creation retry. Check orphan/nonempty preservation, independent checksum/provenance mutations, competing writers, native stale-review/dirty-baseline refusal, unknown-original inspection acknowledgement, CLI precedence, stop before start and no late plan publication. PTY uses the actual settings shell with a synthetic selected set. This does not qualify whole-collector exclusivity, true parent-death/quarantine, real storage duration, physical durability or ordinary-runtime deployment.

[P-08h](../energy-collector.md) adds true synthetic-parent termination with Windows owned jobs/Linux parent-death signals, both holder and outstanding guard exit, refusal of a second live parent, operation-lock takeover exclusion and delayed old-epoch rejection. Verify fresh session/epoch, exact older Prepared retry, frozen policy/binding/budget/time guards, maximum envelopes, corrupt/rechecksummed fence preservation, no-ack/extra-output/helper-exit and stop-before/during startup. Native leased-owner PTY covers private original review and exact retry. These process-level checks do not establish kernel/D-state/disk/power-failure or all-helper quarantine/production deployment qualification.

[P-08i](../energy-runtime.md) tests startup mutation exclusion/post-Provision reload, frozen current-parent OFF/ON, idle/CLI suppression, exact selected-set admission, callback revocation and missing/wrong helper/corrupt store preservation. Execute actual host CLI settings and synthetic-supervisor transitions through a controlling PTY, check public stats/private-default rendering, Unicode/spaces sibling resolution, no PATH/CWD fallback, non-TTY no-I/O and native fixture-hook exclusion. Verify the development CMake install component separately from release qualification. No synthetic worker or copied-host layout proves CUDA, physical sensing, production loader trust or storage duration.

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
