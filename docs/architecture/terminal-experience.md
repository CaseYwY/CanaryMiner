# Terminal dashboard and settings contract

## Pages and ownership

Main, Details, Log and Settings are native terminal pages, not an independent GUI. The renderer consumes snapshots; network and privileged work never runs from a render callback. Keep a noninteractive CLI/config path and an idle settings command.

Main-panel sections, when data is enabled/available:

```text
Session / connection / hardware / current throughput
Accepted: rolling 24h | 7d | 30d | all local history
Price: last-1h average | latest quote | currency/source/age
Today's full-day forecast: tokens | USD reference value
Actual credited 24h: tokens | at-latest-price reference value
Actual cumulative: tokens | at-latest-price reference value | coverage
Estimated net today (USD): only when electricity is configured
Existing user-share / developer-donation counters and events
```

Do not turn unavailable/stale data into zeros. Keep missing-data and attribution reasons accessible. Actual token quantities remain facts; their fiat columns are clearly current reference valuations. Do not show a current-holdings or realized-profit number without the separate evidence that would be needed to establish it.

Short terminals scroll sections rather than silently lose critical state. Keep stop/safety/status and navigation discoverable. Existing TH/s presentation must not silently change the underlying T MAC/s unit or statistics semantics; any corrected unit label needs compatibility/documentation tests.

## Navigation

- S opens Settings; arrows/Tab move focus; Space toggles; Enter edits/chooses/actions; V/F2 reviews; explicit save commits; Esc cancels/returns.
- Settings shortcuts are contextual. Typed Q/D/L/S inside a value editor or search field must not accidentally navigate or quit. Ctrl+C remains an intentional session stop.
- Dirty leave offers review/save, discard or continue. Errors remain beside the relevant field without destroying unrelated drafts.
- Saving never implicitly starts mining, a game, a calibration sweep, a network-account login, hardware tuning or a trade.
- Disabled/unimplemented fields show their reason, not an apparently effective switch. CLI overrides and unsupported platform capabilities remain visible.

## Settings groups

| Group | Controls and distinctions |
|---|---|
| Display | refresh/color/events/worker label; metric-section visibility; show/hide Details and Log |
| Details | identity/bus fields, pool state, restarts/errors, data provenance/coverage, optional per-credit reference observations |
| Log | page enable, severity threshold, displayed rows, timestamp visibility/format, follow/freeze; storage retention is separate |
| Automatic mining | independent enable, eligibility/margin, policy evaluation, pause/resume/switch limits, operating schedule and device scope |
| Electricity | explicit plan/currency/timezone/reset/effective revision, supported cost rules, opening state and estimate basis |
| Market and timing | adapter identity, manual/offline mode, quote/FX/reward refresh, display 1h and optimization lookbacks, cache/retry limits |
| Profiles/tests | local profile validity, warm-up/window/repetitions, time/energy budget, authorized start/cancel, revalidation policy |
| Games | off/pause/coexist, detection/manual trigger, debounce, per-game profile, allowed measurement and FPS/frame-time limits, safe fallback |
| Pools/TLS | secure/strict mode, verified endpoints, consented per-pool TCP fallback, current encryption/error state |
| Language | locale pack, review/fallback status, formatting and terminal/RTL capability; independent of tariff/currency |
| Safety/privacy | temperature/fault limits, separate hardware permission, data retention/collection/export preview, sensitive-section hiding |

A disabled Details/Log page has no advertised shortcut and does not respond to its hotkey. If the active page is disabled, return to Main. Display filtering must not secretly discard logs, suppress safety actions or change recording retention.

Structured log severity must originate from trusted structured events. Legacy raw lines have an explicit unclassified treatment; do not invent severity by brittle substring matching. Timestamp removal only removes recognised renderer-owned timestamp prefixes, not arbitrary user text. Freeze/follow is a presentation state, not permission to block reading worker pipes.

## Change activation

Classify each setting as live presentation, safe-boundary policy re-evaluation, reconnect/restart required or separate permission required. Show before/after and which devices/connections are affected. Stage a valid full change set; detect external edits and unknown newer versions. Save only to a selected local target, atomically, with migration/backup policy.

Display preferences now write schema v2, accepting old v1 files with documented defaults. Loading never rewrites an old file; the first explicit v2 save preserves a validated `ui-preferences.json.v1.bak` rollback copy without overwriting it. Unknown/newer schemas and invalid rollback files fail safely. Financial records and provider credentials are not added to this presentation file.

P-03c [runtime controls](../runtime-settings.md) are available through Automatic mining and Privacy as a separate native subpage/save target. Numeric input has contextual shortcut handling; host wait/history defaults use runtime schema v1, visible CLI precedence and next-explicit-start activation. A current session and its child retries keep their original resolved values. Saving runtime defaults does not commit an outstanding UI draft, restart mining or delete history. This does not enable the unfinished automatic-mining group.

P-06c [manual/offline references](../manual-market.md) are available through Market and timing with their own schema-v1 save target, per-reference UTC input, explicit now-stamp, validity and FX-skew controls. Default off; review identifies live terminal-only activation versus next explicit session and `--no-market` precedence. Values are user declarations, never exchange observations or a complete 1h mean. Saving preserves reference times and separate UI/runtime drafts, and cannot start networking, restart workers or enable financial/policy actions. Genuine storage-stall deadlines remain a release gate for the shared synchronous small-preference path.

P-05b/P-08b [session energy controls](../session-energy.md) are available through Electricity as another separate save target: collection/display, observation selection/gap, declared flat-energy-only exclusions, identity/currency/version, UTC/fixed-standard reporting clock and effective UTC boundaries. Review distinguishes frozen current collection/tariff, configured next-session values and `--no-energy`; all changes wait for a new explicit parent session. The energy section reports board basis, source/origin, model/time/coverage and optional known priced cost, never a utility bill, whole-PC/incremental measurement or net forecast. Unknown complex rules/clock schemes are refused, not flattened or inferred.

P-05c [combined-tariff declarations](../tariff-declaration.md) add a second Electricity entry. Enter descends into native fields/tables; A adds blank rules, X confirms draft removal, Esc ascends/cancels. Review includes all values and changed paths; separate Save persists only a declared whole-meter opening and supported composition. Fixed contract fields are marked, blank quantities are missing, field errors retain/focus drafts, and a successful Save explicitly reports no runtime activation. No bill/reset ledger or measured household state is inferred.

P-07d [credit recovery review](../credit-review.md) is a third Privacy field, not an ingestion switch. Default empty callbacks show unavailable and never open a store. Injected immutable snapshots distinguish tickets/durable input/current proof/unknown outcomes; identifiers and original quantities/times require an explicit local reveal. R→I→Space→Enter reviews then admits one exact original retry under an atomic generation/revision check. Esc/Q/Ctrl+C, persistent acknowledgement focus, no duplicate Enter and separate dirty UI drafts remain native terminal behaviors; no deletion/rebuild/automatic retry or production source activation is added.

P-07e [cursor review](../credit-cursor.md) distinguishes committed checkpoint status from retained pending BEFORE/AFTER. Opaque tokens are hidden by default and fully scrollable after explicit reveal. A pending target, queue ticket or successful inspection cannot authorize another page while reconciliation/stop/fault is active. Exact reviewed retry can atomically commit the original facts and cursor; manual cursor edits are unavailable.

Implemented Log controls add renderer-owned **observed** UTC timestamps; they do not remove or reinterpret timestamps embedded in raw worker text. Default follow tracks the latest output; Up/PageUp holds scroll and F resumes. Turning follow off freezes that page's current snapshot while background collection continues. Re-entering the page takes a new frozen snapshot. Unclassified raw lines have an independent inclusion option; they are not guessed to be warnings because their text contains a keyword.

## Accessibility and internationalization

First milestone locale IDs: `en`, `zh-Hans`, `zh-Hant`, `es`, `pt-BR`, `fr`, `de`, `ru`, `ja`, `ko`, `ar`, `hi`, `bn`, `id`, `tr`, `vi`.

Use reviewed message keys, plural/placeholder validation, English fallback and explicit machine/human review status. Never byte-truncate UTF-8; test graphemes, combining marks, wide cells, RTL and mixed ASCII URLs/numbers. Support no-color/ASCII/plain alternatives; color is never the only error/state cue. The TUI alone does not establish screen-reader compatibility. Do not copy another CLI's branding or assume its dependencies are appropriate for the miner.
