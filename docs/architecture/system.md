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

[P-03d/P-11i](../safety-telemetry.md) supplies a separate default-off raw telemetry owner, strict private selected-UUID batch protocol and parent-bound read-only NVML helper. Driver load/read/shutdown/unload stays off the control thread; any failed field withdraws the complete batch. Immutable original times, checked cleanup and late-Unreaped retention preserve uncertainty. This is not a complete critical-safety provider: threshold/cooling/capability qualification and source-to-fleet admission remain unbound.

[P-03e/P-11j](../core-thermal-guard.md) adds a pure one-session core-temperature limiter over those snapshots. Explicit frozen threshold pairs, required event masks, full ordered device identity and fresh consecutive cooling govern a separately expiring CoreWithinLimits result. Repeated receipts cannot extend its deadline; source stop/rollback/capability changes latch. Complete critical-safety evidence, actual threshold/source qualification and the native fleet bridge remain separate.

[P-11k](../evidence-deadlines.md) supplies independent earliest-ceiling retention for the game runtime's detection/profit and the worker session's safety evidence. Revision increments without a new original time cannot renew expiry. This closes a decreasing-clock-skew hole in the earlier lease arithmetic; source/session validation and native qualification remain separate boundaries.

[P-03f/P-11l](../supervisor-sampling.md) gives the ordinary supervisor one fixed background display reader for local HTTP/JSON and display NVML. Its single in-flight-or-result batch carries frozen worker generations and original clocks; the control loop only consumes in-memory results. Stop cancels publication before worker drain, and native cleanup joins afterward. Ordered original-time energy handoff defers capture clock advancement while a batch is outstanding. This display reader cannot supply complete safety evidence.

[P-03g/P-11m](../safety-fields.md) extends the isolated safety owner with a frozen core-only/extended profile. The v2 profile requires six individually checked native fields and preserves original microsecond time/type/value; any missing result withdraws the entire batch. A pure extended limiter applies separately declared core/memory hysteresis across the complete selected set and freezes field capabilities. It supplies no WorkerSafetyEvidence adapter or ordinary fleet activation.

### M-HISTORY

Owns durable accepted-count observations and rolling queries. Inputs are generation-aware cumulative **user** outcome counters; outputs include counts, storage health, coverage and timestamp basis. It does not infer rewards or mutate pool share counters. A damaged history store cannot justify restarting or retuning a worker.

### M-MARKET

Owns asset-aware price/reward/FX adapters, bounded asynchronous fetches, source health, coverage and cache expiry. Interfaces return typed facts plus provenance. A URL or response cannot supply a shell command, GPU command, wallet key request or executable plugin. Offline/manual providers use the same validation contract.

The initial [P-06c manual references](../manual-market.md) stop short of that provider contract: separate user declarations project terminal text only, never a qualified observation, reward, credit or policy input. Public-feed ownership remains unactivated. Explicit small-preference saves follow the existing synchronous settings path and retain an OS/disk-stall release gate; no frame performs file/network access.

### M-LEDGER

Owns attributable credit facts and source revision/deduplication. The main panel derives latest-price production valuations without mutating these facts; optional historical price observations support a separate Details reference. Actual credits, payouts, wallet balances and sales are distinct record types. It does not derive token income from accepted counts, infer sales or assign wallet-wide income to a worker without evidence.

### M-FORECAST

Owns today's scenario forecast and estimated USD net-profit views. It combines actual credited portions, eligible future productive time, revenue assumptions, tariff/energy results and valuation coverage; it never edits actual credit facts. It emits a missing-data reason rather than a false numeric total.

[P-08j](../income-forecast.md) supplies pure fixture-qualified composition: exact civil-day/posted-ms windows, coherent committed-read identities, complete matched user-work calibration and explicit future donation/pause/switch slots. Immutable reward ratios remain exact through current-price and same-horizon scenario-cost arithmetic; display alone rounds. The ordinary native `FinancialView` accepts prevalidated exact lines, exercised only by the synthetic preview. Real source/work qualification, a coherent helper adapter and bounded monotonic lifecycle owner, actual accrued bill continuation and production activation remain separate.

[P-08k](../forecast-owner.md) supplies strict existing v2 query helpers and a default-off background owner. SQLite READONLY and query-only journal access cannot create/apply/settle source facts. Up to 17 four-query flights must share the same committed source head; read tickets never become evidence. One bounded job, a total read deadline, dual-clock latch, explicit source/profile invalidation, expiry-aware in-memory revaluation and pre-drain revocation protect the optional supervisor seam. Ordinary startup still has no source-qualified forecast factory; real data/work qualification, notifications/settings, accrued bills and deployment remain open.

### M-TARIFF

Owns immutable plan revisions, local billing state and pure counterfactual evaluation. Components include supported energy rules, calendars, taxes, minimum bills and optional stateful settlement extensions. Plan adapters declare capabilities; an unsupported tax/demand/prepaid/export rule must not be replaced by an unlabeled national average.

[P-05g](../billing-ledger.md) supplies exact prefix accrual and an independent synchronous fixture-only monetary store. Complete original chunks, recomputed state and a chained head share one transaction; immutable source/plan/cycle/budgets begin at the explicit zero origin. READONLY inspection and exact replay preserve uncertainty. One committed prefix can seed complete-remainder scenarios without recording them as observations. No renderer/supervisor calls the new library; observed source/checkpoint adaptation, pending recovery and bounded isolated ownership precede runtime activation.

[P-05j](../billing-source.md) adds new source-bound schema v2 and an exact-query receipt adapter. The next monetary prefix must prove the last energy checkpoint's ancestry under the source lease; an increasing sequence alone is insufficient. Source query/original reply, exact watts, money and last source checkpoint share one monetary transaction and replay audit. Previously committed receipts do not depend on the live source; new imports do. This remains synthetic and synchronous pending durable monetary recovery and isolated ownership.

[P-05k](../billing-pending.md) adds a synchronous ordered recovery-store facade: complete canonical binding/chunk frames are durable before monetary apply, and exact source receipts survive process loss. QueryOnly returns committed prefix and pending input separately. Explicit retry checks the inspected revision and full original, never re-fetches and never clears failed uncertainty. This precedes bounded monetary transport/background ownership.

[P-05l](../billing-query.md) adds a parent-bound existing-only monetary reader: it verifies the caller-approved binding digest, audits under ordered leases and returns 48-KiB byte pages under one complete-view hash. The client reconstructs full original and committed state only after all bounded pages agree. Native periodic ownership and isolated write/retry transports remain unconnected.

[P-05m](../billing-retry.md) separately isolates exact existing-original recovery. The helper checks binding/revision/original digests before writable ledger access; it never stages new input. A compact confirmed-state certificate is reconstructed against the parent's full reviewed original. Lost replies and failed retries preserve uncertainty for later readonly inspection.

[P-05n](../billing-input.md) adds a separate staged-input helper under stage→journal→ledger leases. Manifest-bound pages durably preserve up to a full 1-MiB binding plus 128-KiB chunk without changing transport ceilings. Issued precedes new monetary work; Complete requires exact outcome proof. Explicit submission reconciliation only reads monetary facts; ambiguous creation is preserved. Native ownership remains unconnected.

[P-05o](../billing-input-query.md) separately isolates staged-original reads. An approved complete binding lets the parent reconstruct exact staged prefix bytes without retransmitting that binding. One digest binds up to four pages; partial originals, stored outcomes, absent stage and temporary-slot ambiguity remain explicit. It opens neither monetary nor energy stores and supplies no restart action.

[P-05p](../billing-owner.md) serializes those helper clients on one default-off background owner. Stage/current-money/stage observations are revalidated against the reviewed generation/revision before explicit action. Complete input/retained intent/confirmed action proof stay separate from current monetary views. Stop and latched faults prevent revival; native integration and source compute remain unconnected.

[P-05q](../billing-review.md) connects the borrowed immutable owner view to the existing terminal settings, with bounded private document caching and viewport-only rendering. Default-empty access performs no store activation; optional callbacks admit background actions and revoke on session stop. Source notification/compute and production runtime remain unconnected.

[P-05r](../billing-compute.md) supplies one default-off readonly notification/query/compute owner. Its immutable candidate freezes the monetary review, full source receipt and hypothetical next prefix; notification hints are not evidence. It has no financial write authority. Native candidate review and runtime source/configuration wiring remain subsequent integration steps.

[P-05s](../billing-candidate-review.md) borrows both owners through native candidate/recovery views. A one-use compute claim executes after bounded original reconstruction and the final monetary check, under monetary→compute lock order. Later helper preflight/CAS and independent confirmation remain authoritative. The UI propagates memory snapshots and exposes only explicit recompute/Submit; runtime source wiring remains separate.

### M-ENERGY

Owns timestamped power/energy adapters and integration. Board, whole-system and matched incremental-wall observations are different bases. It rejects invalid units/gaps instead of assuming idle/missing power is zero. Shared measurement cannot be assigned to every GPU independently.

[P-05h](../energy-trace.md) adds synchronous audited READONLY access and bounded exact complete-coverage traces, joining raw tails with compacted aggregate intervals. Source binding, head, totals and power chronology share one cached committed model. It preserves basis and all-channel coverage; helpers/owners must not replace time-resolved tariff input with average watts or mistake the returned head for recovery/collection authority. Existing write IPC and runtime entry points are unchanged.

[P-05i](../energy-trace-helper.md) composes this query with an immutable recovery inspection inside a parent-bound one-shot helper. Ordered journal/source leases provide one coherent reply; QueryOnly refuses all journal mutation, and uncertain/divergent recovery blocks complete trace publication. The client is synchronous background-only infrastructure, not a monetary owner or default runtime activation.

The initial [P-05a/P-08a core](../electricity-core.md) is synchronous/pure and unused by the production supervisor. Explicit optional offline IANA data, rational W·ms/costs and declared full-remainder scenarios do not replace a bounded telemetry/economic owner, persisted billing-state adapter or qualified supplier plan. No core calculation belongs in the render/mining/safety loop.

[P-05b/P-08b](../session-energy.md) adds a separate default-off in-memory session owner. The supervisor only admits fixed-size generation-bound raw integer-milliwatt/absence observations from existing NVML reads; exact accumulation and prevalidated view formatting are background work. Its fixed GPU-board basis, explicit origin, UTC mapping guard and drop continuity cannot become whole-meter/baseline evidence. Reviewed flat-energy defaults are frozen per parent session; the full bill/time core, source-qualified forecasts and persistent billing state remain separate. Existing synchronous preference saves/native driver calls still need OS-stall qualification.

[P-05c](../tariff-declaration.md) introduces an independent offline declaration store and native nested editor. It reuses structural core validation, retains declared-forecast whole-meter provenance and never feeds the runtime energy owner. Configuration/opening assertions are persisted with cooperative version/CAS checks, not as measured billing transitions. A source-bound observed/reset ledger and isolated persistence owner remain separate work; synchronous declaration save is not a hard disk-stall deadline guarantee.

[P-05d/P-08c](../energy-history.md) supplies that initial separate observation/reset module: one immutable source/channel binding, explicit parent-session identity, exact power/coverage intervals and independent reset-window projections. Raw input and committed sequence/hash chain share one transaction; a bounded single-flight helper owns all storage. [P-05e/P-08d](../energy-recovery.md) adds a prefix-bound durable original/receipt, default owner inspection and exact reviewed retry across parent reconstruction. [P-05f/P-08e](../energy-compaction.md) adds explicit new-store v2 exact aggregate checkpoints, atomic old-detail expiry and retained current continuity/receipt proof. Existing session-energy preferences do not opt in to this new persistence. Native binding/consent, producer batching, real-cadence storage qualification and monetary bill-state integration remain separate. Reset windows do not mutate or declare the whole-meter opening used by tariff scenarios.

[P-08f](../energy-capture.md) supplies the optional supervisor producer seam: independently authorized frozen plan, private namespace-salted sorted UUID binding, explicit fresh parent identity, at most 128 buffered readings and one helper flight. Startup/uncertain recovery blocks sampling; continuity is assigned before buffering and advances on losses, including failed reads between selected timestamps. Native recovery callbacks are memory-only and exact retry binds the owner generation/revision. The ordinary factory stays empty pending saved authorization, secure identity provisioning and store enrollment; an existing session toggle cannot supply that permission.

[P-08g](../energy-authorization.md) now supplies helper-isolated canonical consent CAS, system-random identity provisioning, one selected-set/immutable-retention registration and injected native controls. Pending identity precedes CreateNew; exact Existing inspection/confirmation is the only completion path after an unknown creation. Fresh successful issuance hands off one frozen plan; Load/last-reserved IDs, CLI disable, uncertainty and stop never grant sampling. Ordinary binding still awaits lifetime-exclusive collector admission, lifecycle/packaging and physical qualification; operation leases are not a whole-collector mutex.

[P-08h](../energy-collector.md) adds the helper-held lifetime/operation locks and transaction fences. [P-08i](../energy-runtime.md) composes those owners once in a default-off native development runtime. The leased capture completes its startup decision before the editable owner reads a fresh CAS. Idle/CLI modes construct no collector, UI callbacks cannot Provision, and stop freezes both private snapshots before post-drain joins. OS-module sibling resolution and development install rules are fixed; no new private state enters public stats/log serialization. These advance integration while keeping production deployment and real telemetry/billing qualification open.

[P-07d](../credit-review.md) connects a native recovery panel through optional fixed-owner in-memory callbacks, default empty in production. `uncertainBatch` aliases the immutable original retained job, including after lost replies. The new `retryReviewed` compares generation/revision/state and performs admission under the same mutex; UI-level last-frame checking alone is insufficient. Rendering never discovers/opens a credit store, refreshes source facts or performs helper I/O. Real collection and production binding/helper packaging remain separate gates.

[P-07e](../credit-cursor.md) adds optional source-bound cursor steps to a new v2 ledger stream. Fact/cursor updates share one transaction, while the durable original intent/receipt remains a separate recovery protocol. The parent has no cursor file or cached database handle: `submitPage` requires the exact generation/revision and idle committed checkpoint. At most one cursor job is outstanding; stop/fault blocks further admission. The fixture contract does not qualify a provider's pagination or supply a fetcher.

### M-PROFILES

Owns bounded measurement plans, profile eligibility/fingerprints, drift detection and evidence. It requests safe session transitions but cannot override user stop, thermal limits or a game's test permission. Imported/model-based curves are hints until locally validated.

[P-09a](../work-profile.md) implements the offline fixture audit and immutable exact work/energy profile, with full identity/plan fingerprints and an explicit-session eligibility latch. It has no sampler, drift detector, persistence, controller or runtime activation; those remain separate capabilities.

### M-OPTIMIZER

[P-10a](../economic-study.md) supplies an immutable offline exact comparison over one audited profile, full-record-bound reward calibration, complete correlated market windows and a restricted flat board-cost adapter. It retains all permitted candidates and selects only a strictly positive best measured score; it has no runtime controller or handoff authority.

[P-10c](../economic-study-use.md) retains one such study under the explicit parent token and current full-plan/condition checks. All original dependency expiry intersects one non-extending exclusive monotonic ceiling, including UTC-only bounds mapped at evaluation rather than first consumption. Failures/stop latch and clear the selected reference point. This pure gate does not accept asynchronous result tickets or actuate a worker; the owning runtime's frozen configuration/source revisions remain a separate boundary.

[P-10d](../economic-evaluation-owner.md) owns the three-lane scheduler and one fixed CPU calculation thread. The control thread alone reads SessionIntent, validates current context/conditions and admits the exact returned job through schedule completion and EconomicStudyUse. A single mailbox retains queued/running/completed ownership; cancellation/expiry never proves completion. Market/retest execution stays with separately permitted owners, and their pending tickets survive evaluator stop. No ordinary supervisor factory or mining actuator is activated.

[P-03h/P-10e](../worker-wait.md) extends the actual worker boundary with optional 0–1000-ms wait installation and original WaitApplied receipts. The CPU computation and worker protocol remain distinct owners: no study automatically issues a command, and command Accepted does not establish application. Selector/measurement integration must bind the exact decision, complete plan and original worker receipt before using the boundary time.

[P-10f](../economic-choice.md) supplies pure exact resume/switch-margin arithmetic over the immutable study and an explicitly asserted current mode. It produces reference-only Hold/Resume/Switch or reasoned pause/unavailability. This does not validate the asserted application or replace result/receipt lifetime ownership, temporal hysteresis or switch budgets.

[P-10b](../optimization-schedule.md) adds a noncopyable single-owner request clock borrowing explicit session intent. Its three bounded lanes retain unresolved expired tickets until settlement; stop/clock/Unreaped faults revoke without inventing reaping. The scheduler neither executes requests nor publishes financial/mining authority.

Owns per-elapsed-time candidate scoring, admissibility, hysteresis, switch budgets and pause/resume decisions. Normal/game candidate sets are separate. It consumes fresh inputs and uncertainty; it cannot turn an unverified model into a guaranteed profit statement.

### M-GAME

[P-11a](../game-session-guard.md) supplies a memory-only episode guard and immutable synthetic ten-minute net windows. Fresh revisioned detection controls exit debounce; nonpositive estimated net locks the episode until confirmed exit. Unknown data, source/time mismatch, safety and explicit stop withhold eligibility. No detector, metrics capture or mining actuator is activated.

[P-11b](../game-detection.md) adds an opt-in isolated native detector and single-use background owner. Exact basename/adapter selection, salted process creation identities and request-bound original scan clocks remain private. Partial/native permission failures are Unknown; cached results cannot renew exit evidence. The ordinary factory and miner actuator remain absent.

[P-11c](../game-runtime.md) owns that detector and the episode guard by value for one explicit session token. Original snapshots reach the same guard throughout its lifetime; source/clock faults cancel detection, and stop revokes before join. The live detection-ownership accessor is separate from historical guard evidence. Supervisor admission and cooperative worker actuation remain subsequent integration.

[P-11d](../work-pause.md) supplies a default-null borrowed control in the shared run loop, with drain-before-pause and refresh-after-resume boundaries. User/donation hooks freeze productive accounting and preserve the remaining no-work timeout. Internal control faults terminate the outer loop without restarting or masquerading as fee-pool failure. Native settings and the fenced parent/worker channel remain separate.

[P-11e](../worker-control.md) adds a strict bounded private protocol, one-flight parent issuer and serialized worker memory gate under frozen session/worker keys, epoch/slot/generation and original command deadlines. Received and Paused receipts stay distinct; expiry/stop cannot revive, and a renewed hold does not alter productive accounting. Production pipe ownership, supervisor/game composition and native settings remain subsequent integration.

[P-11f](../worker-process.md) supplies inherited nonblocking channels separate from logs, one-shot helper-side entropy, single-use process ownership and a background facade. Reaping/quarantine remain distinct from command receipts; startup/IO cancellation never automatically renews or creates mining intent. An opt-in native child binds the client before device discovery; ordinary supervisor/game composition remains next.

[P-11g](../game-worker-session.md) adds the default-empty supervisor fleet seam and a fixed parent-owned game/identity controller. Async identity provisioning, fresh incarnation keys, source-validity-clipped commands and actual-exit result admission span worker retries without rebuilding the guard. Stop precedes drain/join; late Unreaped remains observable. Native safety providers/settings/CLI activation and isolation of remaining synchronous display/settings operations remain subsequent work.

[P-11h](../game-settings.md) adds a separate private schema-v1 store and pure native Games editor. Six fields map to the existing policies without granting runtime authority; the supervisor receives frozen context independently of editable saved defaults. Ordinary startup loads once and refuses non-Off requests before device discovery until qualified safety/fleet activation exists. The existing synchronous small-file persistence path still requires isolation/physical-stall qualification.

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
