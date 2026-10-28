# Execution plan and resumable handoff

## Contribution batches

Task order below is a dependency plan, not a promise that every module lands in one PR. TLS is independent and should be discussed with upstream early even though its stable task ID is P-12. Split large tasks into smaller reviewable commits/PRs without changing their requirement IDs.

| Task | Deliverable | Dependencies | Baseline status |
|---|---|---|---|
| P-01 | Native settings shell, four local presentation controls, idle command and host/PTY tests | Existing upstream renderer/config/supervisor | DONE at ab208e8 |
| P-02 | Details/Log switches and content controls; metric presentation contract | P-01 | DONE at a2549c3; live financial/history integrations are separate tasks |
| P-03 | Typed activation/override contracts, explicit intent arbiter and safe host-side throttle | P-01 | PARTIAL: P-03a stop safety, P-03b host wait and P-03c native persisted defaults/CLI review; live activation, broader config review and native GPU qualification remain |
| P-04 | Durable accepted history with coverage/recovery and 24h/7d/30d/all display | Storage ADR, P-02 | DONE local observed-counter slice; see history validation, not complete pool history |
| P-05 | Portable tariff primitives, state, counterfactual costs and validated plan adapters | Time/decimal ADR, researched rule evidence | PLANNED |
| P-06 | Replaceable bounded price/FX/reward providers, fresh 1h/latest quote snapshots | HTTPS/backend ADR, typed contracts | PARTIAL: P-06a HTTPS/isolation; P-06b exact adapters, bounded cache owner and native projection; live qualification, manual input and supervisor activation remain |
| P-07 | Attributable actual-credit ledger, current production valuation and optional historical references | Storage/decimal ADR, P-06 | PLANNED |
| P-08 | Energy integration, today's forecast and optional estimated net-USD view | P-05, P-06, P-07 | PLANNED |
| P-09 | Bounded local curve measurement and invalidation | P-03, P-08 | PLANNED |
| P-10 | Economic candidate selector and independent schedule controls | P-03, P-05 through P-09 | PLANNED |
| P-11 | Game detection/pause, then experience-qualified coexistence and auto transitions | P-03, P-09, P-10, permitted metrics adapters | PLANNED |
| P-12 | Verified Stratum TLS, explicit fallback, config/packaging/migration and protocol tests | Maintained transport/trust ADR, verified endpoints | PLANNED; independent contribution candidate |
| P-13 | Internationalization foundation plus all first-milestone locale packs | Unicode/message ADR, P-01; parallelize translation review | PLANNED |
| P-14 | Cross-platform privacy/export/release/long-run qualification and upstream submissions | Every shipped feature's own tests; maintain throughout | PARTIAL process, not a release qualification |

## Current handoff

- The fork contains P-01 at `ab208e8`; its source/tests and honest limitations are in `docs/settings-validation.md`.
- The working development branch is `feat/dashboard-metrics`. Architecture and tariff research were drafted after P-01 and are not evidence that P-02 onward is done.
- The final user-approved money decision is **latest-price main-panel valuation**; earlier per-credit-default proposals are superseded. Optional per-credit historical references are secondary and never sale records.
- P-02 at `a2549c3` adds tested D/L switches/content controls, v1-to-v2 preference migration and a synthetic-input financial renderer. Evidence: `docs/display-validation.md`. It does not supply live prices, credits, history or tariff computation.
- P-04 at `b67c4c0` adds optional system-SQLite accepted-counter history, background persistence, window/coverage display and `--no-history`. Evidence: [history validation](../history-validation.md); semantics/recovery: [accepted history](../accepted-history.md). Runtime settings and actual pool-credit data are not supplied by this slice.
- P-03a at `4c4a3e8` adds [session/launcher safety](../session-intent.md): stop tokens, no initial spawn after stop, latched exit 8, forced exit 7 and bounded runtime-only launcher retries. The policy arbiter's game/economic inputs are contracts, not live automation.
- P-03b adds a [complete-work-batch host wait](../work-wait.md), default 0 ms, bounded cancellable CLI control with research/evidence conflicts rejected. P-03c supplies its initial native default controls; production GPU wait/energy qualification remains pending.
- P-03c adds [native runtime defaults](../runtime-settings.md) for host wait/history collection, independent save/review, contextual numeric input, external-writer refusal and visible configured/effective/CLI values. Activation is next explicit session only, frozen across child retries; no automatic restart or deletion.
- P-06a adds a [default-off public HTTPS foundation](../public-https.md): curl/c-ares, strict TLS, fixed public request identifiers, bounded JSON/IPC and an owned native helper process so synchronous trust checks cannot block the miner's stop. Native trust auxiliary egress and production packaging remain release gates. No polling or live financial provider is enabled.
- P-06b adds [exact adapters and a bounded market owner](../market-data.md), retained stale facts, independent clocks, finite backoff, generation-safe cancellation and a read-only native projection. Only synthetic candles accept the unqualified minute-start dialect; no supervisor starts the owner or supplies live financial data. The development preview is clearly synthetic and cannot fetch or mine.
- P-06b evidence: Windows focused CTest 28/28 and backend-enabled market/HTTPS 7/7; Windows/Linux each 20,302 decimal, 87 adapter/window and 111 owner checks, plus 1500 oracle cases and 12 native mode/width scenarios. Linux ASan/UBSan and rebuilt display/settings/synthetic-supervisor regressions pass. These are not live/GPU/release qualification; see the linked evidence for failures and limits.
- Next executable batch: P-06 public-contract/terms/trust-egress qualification, explicit manual/offline user input and reviewed native activation. Exact credit-source semantics remain insufficient; build the independently testable ledger/tariff/energy core without inventing credits while provider qualification proceeds. P-03 live activation/config review and P-12 Stratum transport/endpoint decisions remain separate follow-ups.
- No upstream issue/PR or new production binary has been published for these advanced features. Do not replace an existing user's installed miner as an implicit development step.

## Definition of done for each batch

1. Identify relevant requirement, module and test-family IDs; read current source/AGENTS and inspect existing work before edits.
2. Settle blocking semantics/dependencies, add deterministic failing cases where appropriate, then implement the smallest integrated slice. Disabled future fields must remain visibly unavailable.
3. Run focused tests and affected upstream regressions on the applicable platforms. Separate mocks/CPU/WSL from real native GPU/game measurements.
4. Review privacy/security, migration, rollback, data availability labels, stop/safety priority and donation/protocol invariants.
5. Update this book's statuses and exact evidence. `DONE` is scoped to implemented/tested behaviour; missing providers, scopes or platform capabilities stay explicit.
6. Stage explicit source/docs/test files, scan staged tree/history with an untracked local deny-list, review metadata/artifacts and use only approved public commit identity/DCO.
7. Commit/push only to the intended fork branch without force-overwriting unrelated work. Discuss behaviour/protocol changes through an upstream issue before proposing their upstream merge.
8. Leave a clean handoff: commit, tests run/not run, unresolved decisions, next task and any private artifact location retained **outside public documentation**.

## Questions that block implementation, not the whole roadmap

- Accepted-history storage is settled for the observed-counter slice; financial credit exact-number/scope schema remains open.
- P-06a settles the initial HTTPS backend/process boundary, not native-trust egress, release packaging, Stratum backends or supported secure preset/donation endpoints and migration semantics.
- Exact credit-provider attribution and interval granularity: the reviewed Kryptex schema does not establish stable/revisioned attributable credits or exact rolling24h; actual-income rows remain unavailable. Never solve this by silently using wallet totals.
- Timezone/decimal/Unicode implementation dependencies and supported platform versions.
- Permitted game metrics, default user-selected experience limits and available wall/incremental measurement adapters.
- Current, fully verified local utility plan details for any preset; indexed/historical research does not certify a current account's bill.

If a decision is unresolved, implement a clearly bounded interface/test or another independent task rather than inventing financial facts, silently weakening TLS or enabling unsafe tuning.
