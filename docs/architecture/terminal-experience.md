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

Implemented Log controls add renderer-owned **observed** UTC timestamps; they do not remove or reinterpret timestamps embedded in raw worker text. Default follow tracks the latest output; Up/PageUp holds scroll and F resumes. Turning follow off freezes that page's current snapshot while background collection continues. Re-entering the page takes a new frozen snapshot. Unclassified raw lines have an independent inclusion option; they are not guessed to be warnings because their text contains a keyword.

## Accessibility and internationalization

First milestone locale IDs: `en`, `zh-Hans`, `zh-Hant`, `es`, `pt-BR`, `fr`, `de`, `ru`, `ja`, `ko`, `ar`, `hi`, `bn`, `id`, `tr`, `vi`.

Use reviewed message keys, plural/placeholder validation, English fallback and explicit machine/human review status. Never byte-truncate UTF-8; test graphemes, combining marks, wide cells, RTL and mixed ASCII URLs/numbers. Support no-color/ASCII/plain alternatives; color is never the only error/state cue. The TUI alone does not establish screen-reader compatibility. Do not copy another CLI's branding or assume its dependencies are appropriate for the miner.
