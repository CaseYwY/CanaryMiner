# System and module boundaries

## Architecture direction

Keep the portable policy/data layer host-side in the existing C++ product structure. Do not turn the renderer into a controller, copy a personal Python wrapper into production, change kernel mathematics, or require a GUI/browser runtime. Dependency/backend choices not yet reviewed are explicitly open in [decisions.md](decisions.md).

```text
User / launcher / CLI / terminal settings
                |
         Typed configuration + user intent
                |
         Session supervisor / policy arbiter
           |                     |
      Worker lifecycle       Policy evaluation
           |                     |
   Host throttle boundary    Candidate selector
           |                /       |        \
   Unchanged CUDA work   Profiles  Economics  Game constraints
                                   /   |   \
                              Market Ledger Tariff + energy

Read-only snapshots -> dashboard / Details / Log
Local durable stores <- observation and credit events
Network adapters -> validated typed data, never executable instructions
Restricted optional helper -> owned hardware controls only (separate opt-in)
```

## Modules

### M-UI

Owns pure rendering, keyboard context/focus, staged edits and read-only presentation snapshots. Current anchors are `dashboard.h`, `settings.h`, `terminal_keys.h` and `status_view.h`. It never fetches prices, tunes a GPU, runs a benchmark or mutates the income ledger directly.

### M-CONFIG

Owns typed defaults, schema validation/version migration, CLI precedence, persistence targets and change impact. `config.h`, `product_cli.h` and `ui_preferences.h` exist today. Mining config, presentation preferences, private provider credentials and runtime records remain separate concerns. Unknown/new versions must not be silently reset.

### M-SUPERVISOR

Owns explicit session intent, worker generation and lifecycle, safe transitions, host throttle requests, fault/thermal handling and state publication. Extend the existing supervisor instead of nesting an independent watchdog that can resurrect an intentionally stopped miner. The optional privileged helper has an allow-listed protocol and no network work; its process/PID identity must include creation/generation evidence.

### M-HISTORY

Owns durable accepted-count observations and rolling queries. Inputs are generation-aware cumulative **user** outcome counters; outputs include counts, storage health, coverage and timestamp basis. It does not infer rewards or mutate pool share counters. A damaged history store cannot justify restarting or retuning a worker.

### M-MARKET

Owns asset-aware price/reward/FX adapters, bounded asynchronous fetches, source health, coverage and cache expiry. Interfaces return typed facts plus provenance. A URL or response cannot supply a shell command, GPU command, wallet key request or executable plugin. Offline/manual providers use the same validation contract.

The initial [P-06c manual references](../manual-market.md) stop short of that provider contract: separate user declarations project terminal text only, never a qualified observation, reward, credit or policy input. Public-feed ownership remains unactivated. Explicit small-preference saves follow the existing synchronous settings path and retain an OS/disk-stall release gate; no frame performs file/network access.

### M-LEDGER

Owns attributable credit facts and source revision/deduplication. The main panel derives latest-price production valuations without mutating these facts; optional historical price observations support a separate Details reference. Actual credits, payouts, wallet balances and sales are distinct record types. It does not derive token income from accepted counts, infer sales or assign wallet-wide income to a worker without evidence.

### M-FORECAST

Owns today's scenario forecast and estimated USD net-profit views. It combines actual credited portions, eligible future productive time, revenue assumptions, tariff/energy results and valuation coverage; it never edits actual credit facts. It emits a missing-data reason rather than a false numeric total.

### M-TARIFF

Owns immutable plan revisions, local billing state and pure counterfactual evaluation. Components include supported energy rules, calendars, taxes, minimum bills and optional stateful settlement extensions. Plan adapters declare capabilities; an unsupported tax/demand/prepaid/export rule must not be replaced by an unlabeled national average.

### M-ENERGY

Owns timestamped power/energy adapters and integration. Board, whole-system and matched incremental-wall observations are different bases. It rejects invalid units/gaps instead of assuming idle/missing power is zero. Shared measurement cannot be assigned to every GPU independently.

The initial [P-05a/P-08a core](../electricity-core.md) is synchronous/pure and unused by the production supervisor. Explicit optional offline IANA data, rational W·ms/costs and declared full-remainder scenarios do not replace a bounded telemetry/economic owner, persisted billing-state adapter or qualified supplier plan. No core calculation belongs in the render/mining/safety loop.

### M-PROFILES

Owns bounded measurement plans, profile eligibility/fingerprints, drift detection and evidence. It requests safe session transitions but cannot override user stop, thermal limits or a game's test permission. Imported/model-based curves are hints until locally validated.

### M-OPTIMIZER

Owns per-elapsed-time candidate scoring, admissibility, hysteresis, switch budgets and pause/resume decisions. Normal/game candidate sets are separate. It consumes fresh inputs and uncertainty; it cannot turn an unverified model into a guaranteed profit statement.

### M-GAME

Owns detection/manual override, per-game capabilities, experience baselines and pause/coexist policy constraints. It requests allowed miner transitions. It never injects into a process, kills/modifies a game, assumes a launcher is gameplay, or changes whole-GPU settings without the independent hardware-control policy.

### M-TLS

Owns authenticated pool byte transport, handshake/I/O deadlines and explicit downgrade policy. Preserve the injectable `PearlPoolTransport` boundary and Pearl framing above it. HTTPS data adapters may share approved dependency/trust components, but pool transport and data-provider connections have distinct policies and error classes.

### M-I18N

Owns message catalogues, fallback/review status, locale formatting and terminal width/bidirectional rendering. Calculation code, asset identity, stored numbers, protocol bytes and machine-readable API keys never depend on translated labels.

### M-PRIVACY

Owns collection limits, local retention/export previews, credential references and redacted diagnostic bundles. Sensitive values are not routed through a public stats/log channel merely because that channel already exists. Financial and game records need their own opt-in exposure controls.

### M-RELEASE

Owns host/production test matrices, packaged dependencies/licences, compatibility/migration notes and honest release status. Windows and Linux capabilities are reported separately. Existing device qualification and kernel admission are not broadened as an incidental convenience.

## Policy arbitration and session intent

```text
Stopped by user (terminal; fresh start required)
  > latched fault / exhausted retry budget
  > thermal or invalid critical telemetry
  > game policy / required game-experience protection
  > missing or nonpositive economic eligibility
  > normal eligible mining
```

Possible states: `Stopped`, `Starting`, `MiningNormal`, `MiningCoexist`, `Calibrating`, `PausedGame`, `PausedEconomics`, `Cooling`, `FaultCooldown`, `FaultLatched`, `Stopping`. Connection substate and data-source health are separate from the mining state. Do not flatten “no price”, “game pause” and “GPU fault” into one restart action.

- Only explicit start creates desired-mining intent. Configure, import, save, show status, run idle settings, or refresh prices never creates it.
- Windows may offer an optional per-user convenient launcher/Start-menu entry; Linux keeps an equivalent explicit CLI entry. Do not copy a private task name/path or add login/scheduled mining triggers implicitly.
- Economic recovery/game exit may resume only inside that still-authorized session. Intentional exit invalidates outstanding asynchronous results and retries.
- Legacy advanced launchers must distinguish clean intentional exit from a crash. An outer `start-advanced` restart loop or installed service must not undo the supervisor's stop decision; test both process and launcher boundaries.
- A worker switch drains/terminates under the existing bounded lifecycle; preserve shares/carry and mark downtime. Never replay an outcome-ambiguous submission.
- Every background job carries session/generation/cancellation identity. Late results cannot start a new worker, overwrite a newer config or resurrect stale measurements.
- Hardware restoration applies only to settings owned by this session and must not overwrite a later external change. Reboot/driver-reset recovery requires fresh explicit permission, never a price/temperature callback.
- External data, settings rendering and benchmark waits cannot block safety/user-stop processing. Queue/buffer/retry limits are required; shutdown must cancel and close gracefully.

## Durable storage boundary

Use transactional/idempotent history and credit stores with versioned records, one-writer/concurrency policy, bounded retention/compaction and tested crash recovery. The engine must preserve all-time aggregates while pruning expired detail and show the retained-detail coverage.

Local accepted history now uses optional system SQLite behind a single background owner; see [D-17 implementation](../accepted-history.md). This commits counters, lifetime totals and window records atomically and preserves unknown/corrupt/newer stores without resetting them. Production credit-source mapping/activation remain open; the initial fixture schema and isolated owner are described below. Never silently delete corrupt user records, initialize them as a zero lifetime total, or publish them as test fixtures.

The initial [P-07a credit store](../credit-ledger.md) implements a separate fixture-only schema and shared storage-only lease. Its synchronous object is used by the new [P-07b isolated owner](../credit-owner.md) only inside one-operation helpers, never in the production render/mining/safety loop. Per-operation leases, full revalidation and explicit uncertain-write recovery do not complete real provider, pending-journal/UI or OS-stall/release qualification. No supervisor activates it; accepted counters never supply income.

[P-07c](../credit-recovery.md) supplies the initial pending-journal part by default: a helper-owned private sidecar precedes ledger mutation and retains exact prepared/confirmed/rejected input. Queries do not auto-replay; missing/divergent current-ledger proof and failed retries preserve reconciliation requirements. Ledger schema v1 and the no-production-activation boundary remain unchanged; native recovery/source-cursor UI and OS failure qualification remain open.

Presentation preferences already use a small independent atomic JSON store; this is not a justification to rewrite large financial/history files synchronously on each screen frame.
