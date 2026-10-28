# Requirements and traceability

Each row is a testable requirement, not a checklist of already implemented features. Stable IDs are never recycled. Module IDs are defined in [system.md](system.md), task IDs in [execution-plan.md](execution-plan.md), and test-family IDs in [testing.md](testing.md).

| ID | Requirement | Module | Task | Tests | Status |
|---|---|---|---|---|---|
| R-UI-001 | Open native settings with S in the supervised dashboard; keep existing main/details/log workflows. | M-UI | P-01 | T-UI | DONE |
| R-UI-002 | Navigate with arrows and Tab; change supported values with Space/Enter; review with V/F2; save explicitly; Esc cancels/returns. | M-UI | P-01 | T-UI | DONE |
| R-UI-003 | Open the same settings with an idle command without starting mining; reject redirected/noninteractive use. | M-UI | P-01 | T-UI | DONE |
| R-UI-004 | Persist four existing presentation controls atomically, detect stale writers and leave active values unchanged on failure. | M-CONFIG | P-01 | T-CONFIG | DONE |
| R-UI-005 | Independently enable/disable Details and Log pages; hide their entry and reject their shortcut when disabled. | M-UI | P-02 | T-UI | DONE |
| R-UI-006 | Configure Details fields and Log level, rows, timestamps and follow behaviour; distinguish display filtering from storage retention. | M-UI | P-02 | T-UI | DONE |
| R-UI-007 | Show configured/effective/CLI-overridden values and live/re-evaluate/reconnect/permission change classes; detect external config changes. | M-CONFIG | P-03 | T-CONFIG | PARTIAL |
| R-UI-008 | Preserve focus and readable content on resize, short terminals, no-color/plain output and text editing; never trigger global shortcuts inside text fields. | M-UI | P-02 | T-UI | PARTIAL |
| R-HIST-001 | Show rolling 24h, 7d, 30d and all-local-history accepted counts; show collection start/coverage and missing intervals. | M-HISTORY | P-04 | T-HISTORY | DONE |
| R-HIST-002 | Count acknowledged accepted user shares only; exclude developer-donation shares and never convert share count directly into income. | M-HISTORY | P-04 | T-HISTORY | DONE |
| R-HIST-003 | Preserve history across restarts with counter-generation/deduplication rules, bounded storage, crash recovery and explicit corruption handling. | M-HISTORY | P-04 | T-HISTORY | DONE |
| R-HIST-004 | Distinguish observed acknowledgement time from unavailable exact pool timestamps; handle clock jumps without wrapping or double counting. | M-HISTORY | P-04 | T-HISTORY | DONE |
| R-MKT-001 | Display last-1h equal-time average and latest token price with quote currency, source, as-of, coverage and freshness. | M-MARKET | P-06 | T-MARKET | PARTIAL |
| R-MKT-002 | Replace price, FX and reward adapters without changing policy code; validate asset/network identity rather than ticker alone. | M-MARKET | P-06 | T-MARKET | PARTIAL |
| R-MKT-003 | Use bounded asynchronous HTTPS requests, provenance, cache expiry, rate limits, retry/backoff and manual/offline inputs; no trading or private keys. | M-MARKET | P-06 | T-MARKET | PARTIAL |
| R-INC-001 | Show today's full-day forecast, actual credited income in the last 24h and actual cumulative credited income in token and dollar units. | M-LEDGER | P-07 | T-LEDGER, T-FORECAST | PARTIAL |
| R-INC-002 | Define today by the selected billing/reporting timezone; forecast actual credited so far plus remaining-time expected income with assumptions and coverage. | M-FORECAST | P-08 | T-FORECAST | PLANNED |
| R-INC-003 | Credit facts require provider IDs/revisions and attributable scope; missing/incomplete/ambiguous data is unavailable or partial, never invented zero. | M-LEDGER | P-07 | T-LEDGER | PARTIAL |
| R-INC-004 | Never substitute wallet balance, payouts, pending rewards or another worker's/account's income for this miner's credited revenue. | M-LEDGER | P-07 | T-LEDGER | PARTIAL |
| R-INC-005 | Final selected main-panel mode is latest-price revaluation of actual token production; optional per-credit historical estimates belong in Details, not the main total. | M-LEDGER | P-07 | T-LEDGER | PARTIAL |
| R-INC-006 | Label values production reference valuations, never sale proceeds, realized profit or current holdings; USDT and USD stay distinct and USD needs a valid conversion rate. | M-LEDGER | P-07 | T-LEDGER | PARTIAL |
| R-INC-007 | Preserve credit corrections/reversals idempotently; market ticks change derived valuations, never token-credit facts; optimization uses fresh conditions and sale status remains unknown. | M-LEDGER | P-07 | T-LEDGER | PARTIAL |
| R-NET-001 | Omit net-profit rows completely when electricity is not configured; when configured but required inputs are missing, show unavailable/reason rather than zero. | M-FORECAST | P-08 | T-FORECAST | PLANNED |
| R-NET-002 | Allow clearly labelled GPU-board-power estimated net profit in USD; expose power basis, time period, exclusions and uncertainty; do not claim wall-profit proof. | M-ENERGY | P-08 | T-ENERGY, T-FORECAST | PLANNED |
| R-TAR-001 | Separate language, formatting locale, jurisdiction, supplier/territory/plan, currency, timezone, meter arrangement and effective revision. | M-TARIFF | P-05 | T-TARIFF | PARTIAL |
| R-TAR-002 | Compose flat, progressive-block, all-units-band, TOU and signed dynamic energy rules; unsupported compositions must be rejected explicitly. | M-TARIFF | P-05 | T-TARIFF | PARTIAL |
| R-TAR-003 | Model independent billing/reset clocks, season/holiday/event precedence, DST, effective-date changes, proration and plan-specific rounding. | M-TARIFF | P-05 | T-TARIFF | PARTIAL |
| R-TAR-004 | Account for applicable tax/fees/minimum bills without double counting; demand/capacity, prepaid and export ledgers require explicit supported capabilities. | M-TARIFF | P-05 | T-TARIFF | PARTIAL |
| R-TAR-005 | Optimize whole-meter incremental bill difference over the relevant horizon; shared-meter miners and household/generation loads cannot independently reuse allowances. | M-TARIFF | P-05 | T-TARIFF | PARTIAL |
| R-TAR-006 | Keep rules/feeds replaceable and effective-dated with primary-source provenance, review status and expiry; never ship a country as a single tariff. | M-TARIFF | P-05 | T-TARIFF | PLANNED |
| R-ENG-001 | Integrate W over actual elapsed time into energy; support labelled board/system/incremental-wall measurement adapters with timestamp validity. | M-ENERGY | P-08 | T-ENERGY | PARTIAL |
| R-PRO-001 | Measure per-device/software/environment curves with warm-up, repeats, time/energy budgets and confidence/coverage metadata. | M-PROFILES | P-09 | T-PROFILES | PLANNED |
| R-PRO-002 | Invalidate/revalidate profiles on relevant GPU/driver/kernel/throttle/environment changes, drift or faults; do not copy profiles by model name alone. | M-PROFILES | P-09 | T-PROFILES | PLANNED |
| R-PRO-003 | Prefer host-side throttling between complete work units; waiting is milliseconds, not duty-cycle percent; zero is valid. | M-SUPERVISOR | P-03 | T-INTENT, T-PROFILES | PARTIAL |
| R-OPT-001 | Independently opt in to automation; maximize estimated net return per real elapsed time among measured eligible candidates, not peak rate or efficiency alone. | M-OPTIMIZER | P-10 | T-OPTIMIZER | PLANNED |
| R-OPT-002 | Evaluate on explicit start and periodically; independently configure policy interval, market refresh/lookback, telemetry, warm-up/test duration and recalibration. | M-OPTIMIZER | P-10 | T-OPTIMIZER | PLANNED |
| R-OPT-003 | Pause without eligible positive candidates; use bounded resume hysteresis/cooldowns, uncertainty and transition/test cost; do not pretend a heuristic margin is a confidence bound. | M-OPTIMIZER | P-10 | T-OPTIMIZER | PLANNED |
| R-GAME-001 | Independent game policies: off, pause mining, or tested coexistence; support launcher/game/manual detection with debounce and capability status. | M-GAME | P-11 | T-GAME | PLANNED |
| R-GAME-002 | Launcher detection alone proves linkage, not actual game performance; coexistence needs comparable real-game baseline and user experience limits. | M-GAME | P-11 | T-GAME | PLANNED |
| R-GAME-003 | Run only authorized bounded tests; optimize within FPS/1%-low/frame-time/latency/VRAM/thermal limits; safe fallback when unavailable. | M-GAME | P-11 | T-GAME | PLANNED |
| R-GAME-004 | No injection, anti-cheat bypass, game modification/killing, focus stealing or full-rate sweeps during play; prefer miner throttling over global GPU controls. | M-GAME | P-11 | T-GAME | PLANNED |
| R-GAME-005 | After a qualified coexistence profile is enabled, auto-enter it when detected and restore normal eligibility on exit; unprofitability pauses within the started session. | M-GAME | P-11 | T-GAME, T-INTENT | PLANNED |
| R-TLS-001 | Add verified Stratum TLS, secure preference for new setup/presets and explicit strict SSL URLs; retain intentional TCP compatibility with warning. | M-TLS | P-12 | T-TLS | PLANNED |
| R-TLS-002 | Per-pool plaintext fallback is off by default and requires consent plus a configured TCP endpoint; cert/name/trust failures never downgrade. | M-TLS | P-12 | T-TLS | PLANNED |
| R-TLS-003 | Verify trust/hostname, use SNI, bounded cancellable I/O and maintained TLS; do not guess/scan ports or expose credentials before verified handshake. | M-TLS | P-12 | T-TLS | PLANNED |
| R-TLS-004 | Preserve protocol bytes and donation semantics across transport/reconnect/fallback; do not replay ambiguous submissions. | M-TLS | P-12 | T-TLS, T-RELEASE | PLANNED |
| R-SAFE-001 | Intentional Q/Ctrl+C/window-close stop wins over every retry, refresh, economic recovery or game exit; no implicit login/startup mining. | M-SUPERVISOR | P-03 | T-INTENT | PARTIAL |
| R-SAFE-002 | Fault/thermal/telemetry protections remain active during settings/network/tests, with bounded restart budgets and controlled latch/cooldown states. | M-SUPERVISOR | P-03 | T-INTENT | PARTIAL |
| R-SAFE-003 | Existing disabled hardware tuning remains disabled; future controls need separate permission, restricted helper and ownership-safe restoration. | M-SUPERVISOR | P-03 | T-INTENT | PLANNED |
| R-SAFE-004 | Never reboot, reset/reload a display driver or perform other remote-disconnecting recovery without fresh explicit permission. | M-SUPERVISOR | P-03 | T-INTENT | PLANNED |
| R-I18N-001 | First internationalization milestone targets 16 declared locale packs with honest translation review status and English fallback. | M-I18N | P-13 | T-I18N | PLANNED |
| R-I18N-002 | Support UTF-8 graphemes/cell width/combining text/RTL while CLI/API keys, event codes, calculations and asset identity remain invariant. | M-I18N | P-13 | T-I18N | PLANNED |
| R-PRIV-001 | Keep wallet/account/device/path/rate/consumption/game-activity/config data local; collection, provider disclosure, retention and export require explicit controls. | M-PRIVACY | P-14 | T-PRIVACY | PLANNED |
| R-PRIV-002 | Public code/docs/fixtures/commits/artifacts use synthetic data and approved metadata; scan staged files and available history; preserve upstream attribution/licences. | M-PRIVACY | P-14 | T-PRIVACY | PARTIAL |
| R-REL-001 | Support Windows/Linux core parity with explicit capability differences; CPU/WSL tests do not prove production GPU/game support. | M-RELEASE | P-14 | T-RELEASE | PARTIAL |
| R-REL-002 | Keep small reversible contribution batches, issue-first upstream behaviour/protocol discussion, DCO and all required release gates. | M-RELEASE | P-14 | T-RELEASE | PARTIAL |

`PARTIAL` means a baseline behaviour or process exists, not that the expanded requirement is complete. For example, current Q/signal tests do not establish the future economic/game resume state machine, and current ASCII resize tests do not establish multilingual terminal accessibility.

P-04 `DONE` is the local **observed-counter** scope in [accepted history](../accepted-history.md), with [host/PTY evidence](../history-validation.md). It is not complete pool history: missed shutdown tails/session gaps are uncertain, exact pool timestamps are unavailable, and clock warnings suppress rolling totals. Financial-credit storage and persisted runtime settings are separate tasks.

R-UI-007 remains `PARTIAL` in implementation scope: [P-03c](../runtime-settings.md) now supplies native runtime defaults, configured/effective/CLI review, explicit next-start activation and external-writer detection for runtime/UI files. Live policy activation and external mining-config review are not implemented. Income-provider research is [capability evidence](../research/market-and-credit-providers.md), not passing actual-income implementation.

R-MKT-001–003 are `PARTIAL`: [P-06a](../public-https.md) implements strict optional HTTPS/helper isolation; [P-06b](../market-data.md) adds exact price/FX adapters, fixture-only candle conventions, complete-window arithmetic, a bounded explicit-start cache owner and native read-only projection. No production supervisor/settings activation, qualified live candles, custom manual input, reward/credit source or financial income is integrated; native trust auxiliary egress/packaging remain release gates.

R-INC-001/003–007 are `PARTIAL`: [P-07a](../credit-ledger.md) supplies fixture-qualified durable facts, exact coverage/queries, revisions/provenance and pure latest-price references. It supplies no qualified actual-credit source, production/background/UI activation, today's calendar forecast, historical reference display or automatic optimization. Runtime actual-income rows remain unavailable; account scope cannot become worker scope by inference.

R-TAR-001–005 / R-ENG-001 are `PARTIAL`: [P-05a/P-08a](../electricity-core.md) supplies explicit offline civil clocks, exact elapsed energy/coverage, supported declared tariff composition and pure same-opening-state complete-cycle scenarios. No utility preset/source certification, persistent billing/reset ledgers, telemetry adapters, native settings/activation or optimizer is supplied. Calendar foundations do not complete R-INC-002; income/net-USD forecasts remain planned.
