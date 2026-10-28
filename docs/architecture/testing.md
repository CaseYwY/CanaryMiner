# Test families and release gates

All fixtures are synthetic. Acceptance specifications are not passing runtime evidence. Tests must state their measurement/OS/toolchain scope; CPU stubs, WSL, fake launchers and short mining runs cannot substitute for native GPU/game/release qualification.

## T-UI

Test S entry, all keyboard contexts, focus, supported edit/toggle/select, review/save/discard, idle/no-mining command and redirected rejection. Verify D/L page switches and disabled shortcuts, active-page fallback, each Details field, Log threshold/row limit/time/follow display and continued pipe draining. Verify resize/short/no-color/plain and explicit unavailable/stale/coverage labels. Net row is absent without electricity; valuation is never called sold proceeds/current holdings.

## T-CONFIG

Test strict keys/types/ranges, old-version defaults, unsupported-new-version rejection, atomic replace, read-only/invalid/external edits, stale writers, lock/crash handling, CLI precedence and independent credential/runtime stores. A failed save cannot change effective state. Navigation cannot grant benchmark, plaintext or hardware permission.

## T-HISTORY

Test generation resets and carried counters, duplicate/reordered samples, aggregation across GPUs, donation exclusion, accepted versus pending/rejected/unknown, rolling-window exact boundaries, clock jumps and incomplete coverage. Test restart/durable recovery, compaction preserving all-time totals, truncated/corrupt writes, concurrent writer refusal, counter overflow and missing-period reporting. Never infer rewards from shares.

Executed local observation slice: [history validation](../history-validation.md). Includes process-crash/WAL recovery and PTY two-session persistence, not physical power-loss, full-disk or production-CUDA qualification.

## T-MARKET

Test correct/wrong asset and network; latest quote versus equal-time 1h average; full/duplicate/missing/incomplete minute candles; sparse time weighting; pagination; valid zero-volume candles; stale/future timestamps; provider errors/rate limits; cache/cancellation/source switch. Test token/USDT times USDT/USD, missing/depegged conversion and explicit currency labels. HTTP responses are bounded; no redirects to unauthorized schemes/hosts and no disabled TLS verification.

## T-LEDGER

Test actual credits versus balance/payout/pending/share count; worker versus wallet attribution; inclusive/exclusive time boundaries; pagination/retries/revisions/reversals and late discoveries. Test that market updates preserve token facts while changing **latest-price production valuation**. Two differently priced credit events at the same latest price must value as their total tokens times that latest price, not the historical weighted sum. Optional historical estimates remain separate. Sale status stays unknown, even after a transfer/deposit; missing/partial data never becomes zero or complete lifetime revenue.

## T-FORECAST

Test today's actual-so-far plus remaining eligible expected income, calendar 23/24/25-hour days, delayed/incomplete current-day credits, uncertain schedules, switching/donation/fees without double deduction, and the separate rolling-24h row. Test no-tariff row omission versus configured zero/negative rate, missing price/FX/reward/cost data, negative valid estimated net values and matching revenue/cost horizons. Labels must say estimated, not realized.

## T-TARIFF

Implement [research test specifications T01–T34](../research/international-electricity-billing.md#7-deterministic-edge-case-tests-to-implement), including progressive and all-units boundary jumps, shared-register tiers, independent resets, annual reconciliation, signed rates, dynamic coverage, DST/standard-time, event priority, effective revisions, demand averaging/ratchets/joint non-additivity, taxes, minimum floors, prepaid acquisition, export opportunity and restricted/expiring credits. These source-derived families do not certify every jurisdiction's settlement rules.

Add plan-specific official worked-example comparisons before labelling an adapter source-verified. Verify pure counterfactual evaluation from identical opening state and no look-ahead to unpublished prices/events. Reject unsupported current contract/allocation rules; do not smooth real discontinuities or use country averages silently.

## T-ENERGY

Test elapsed-time W-to-kWh integration, irregular samples, invalid units/NaN/negative sensor power, gaps, matched baseline/subtraction, multiple GPUs/shared meters and signed cost conversion. Distinguish missing power from zero power and full-board game load from miner incremental energy. Persist and display measurement basis/coverage.

## T-PROFILES

Test zero-ms throttle semantics and safe complete-work-unit boundaries, warm-up/exclusion, repeated windows, bounded time/energy costs, cancellation and faults. Test profile fingerprints, import hints versus measured eligibility, drift/software/environment invalidation and no full-speed sweep during play. No arbitrary device whitelist or kernel-math expansion as a side effect.

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
