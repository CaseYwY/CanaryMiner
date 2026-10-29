# Independent session-bound optimization requests (P-10b)

M-OPTIMIZER; R-OPT-002, R-SAFE-001/002 and R-PRIV-002; T-OPTIMIZER/T-INTENT; D-61. `OptimizationSchedule` is a default-off, single-owner, memory-only request clock. It borrows an already explicit `SessionIntent` token and never starts a session, network request, measurement, computation or mining process. It is not an automatic consumer of an [economic study](economic-study.md).

## Independent clocks and fixed bounds

The constructor freezes the policy for one instance. `start` is single-use and requires a live explicit session token. First `poll` requests a market refresh and an economic evaluation independently; downstream evaluation may return unavailable while awaiting data. Optional retest review starts after its own interval, not as an implicit startup experiment.

| Setting | Default | Allowed range |
|---|---|---|
| Economic market refresh | 1 hour | 1 minute–24 hours |
| Economic evaluation | 1 hour | 1 second–24 hours |
| Retest-review reminder | Disabled (0) | Disabled or 1 minute–30 days |
| Retry delay | 30 seconds | 1 second–1 hour |
| One request's operation deadline | 10 seconds | 500 ms–60 seconds |
| Additional retry attempts | 3 | 0–3 |

These are scheduling bounds, not provider rate-limit certification or physical test permissions. The market lookback remains in `EconomicPolicy`; telemetry cadence, warm-up, measurement duration/repetitions and measurement budgets remain in `WorkProfilePlan`. No timer silently resets or changes those settings.

There are exactly three fixed lanes and at most one unsettled ticket per lane. Tickets contain the session generation, unique instance sequence, lane, attempt, issuance clocks and absolute monotonic deadline. New work is returned only once. Concurrent ownership is unsupported; the noncopyable instance and borrowed intent must be kept alive/serialized by the eventual owner.

## Completion, expiry and retry

`complete` requires the exact full ticket, including its issuance/deadline. Changed, cross-lane and already-settled tickets cannot affect another flight. Timely Complete settles one request; Retryable may schedule a bounded delayed retry. Rejected or voluntarily Cancelled returns to the regular interval without an immediate retry. RetestReview never receives automatic retries: it is a due reminder to seek/review separate testing authorization, not a physical test job. Its operation deadline bounds handling the reminder, not the human's time to decide.

Regular due time is anchored to that cycle's first issue; retries retain it. When completion/delayed polling has missed a whole regular period, the next request is scheduled one interval forward, with no catch-up burst. Initial attempt plus at most three retries cannot become an infinite tight loop. A new regular cycle has a fresh retry budget.

At the deadline (inclusive), `poll` emits a one-time cancellation request and marks the ticket revoked. **The pending slot remains occupied until ownership is explicitly settled.** Elapsed time does not prove cancellation, reaping or helper death. No replacement can overlap an expired task in the same lane, even across many subsequent periods. An exact completion at/after deadline is marked Expired even without a preceding timeout poll; its result is ineligible, but it can settle ownership and schedule a bounded retry. The actual worker/client must apply its own cancellation and process-lifecycle rules.

`Unreaped` latches the instance Faulted and cancels other lanes; it never opens a replacement slot for useful work. After a user stop, the unreaped fact remains visible while Stopped retains priority. A completion code denotes settlement admission, not successful financial data, current coverage or mining permission; only a timely Complete can be considered by a future independently validated result consumer.

## Stop and clock precedence

Explicit `requestStop`, a stopped intent or a new session epoch permanently revokes this instance. Stop-before-start is terminal. Later completions can settle retained ownership but cannot schedule new work; later explicit session start cannot revive old tickets. Pending cancellation notifications remain available once for the caller to act on.

UTC and monotonic clocks must each be nondecreasing. Their cumulative elapsed difference from startup is limited to two seconds, so repeated small discrepancies cannot renew a stale clock indefinitely. Invalid UTC, reversal, excessive disagreement or deadline overflow latches ClockFault and revokes pending work. User stop takes priority over clock/fault state. No system clock is read inside this module.

The scheduler supplies no durable quarantine/revocation registry. Its future owner must preserve terminal fault and unresolved ownership across object replacement, cancel/reap actual tasks, and enforce permission/rate/safety constraints before executing any request. No worker termination, hardware operation, automatic retraining, data-publication authority or runtime activation is introduced here.

## Verification and remaining work

Deterministic host tests exercise initial/independent timing, all policy bounds, exact retries/budget exhaustion, duplicate/forged tickets, expired-slot retention and late settlement, no catch-up, retest reminders, cumulative clock faults, integer overflow, Unreaped and stop-before-start/late-completion/new-session precedence. Executed evidence: Windows/MSVC focused **3/3**, Windows and Linux/WSL/direct GCC **67 checks** each; the Linux implementation and test also pass **ASan+UBSan**. The architecture guard passes after final document updates.

Next: an independently gated result consumer with freshness/hysteresis/cooldown/switch budgets, lifecycle-owning workers, native reviewed schedule controls and qualified source/live-measurement integration. These request-clock tests do not qualify real helpers, physical measurement deadlines or automated mining.
