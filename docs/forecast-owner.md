# Coherent existing-credit reads and bounded forecast ownership (P-08k)

This joins the [exact forecast core](income-forecast.md) to existing committed v2 credit snapshots through a dedicated query helper and one background read/compute owner. M-FORECAST/M-LEDGER/M-SUPERVISOR; R-INC-001–007, R-NET-001/002, R-SAFE-001/002 and R-REL-001 remain partial; T-FORECAST/T-LEDGER/T-INTENT/T-UI/T-PRIVACY; D-45.

The ordinary native entry point still supplies **no forecast factory**. `ForecastOwnerPolicy.enabled` defaults OFF. The new supervisor seam is exercised with explicitly synthetic source/work/power scenarios and fake workers. It does not qualify a real credit/reward provider or measured work profile, import records, connect to a pool, change mining/donation settings, persist a utility bill, or deploy an income feature into the user's installed miner.

## Existing query path

`canary_forecast_query_worker` accepts only `CMFQ1` containing the parent PID and an existing CMCR3 request. It requires a cursor-qualified fixture binding, Inspect, no batch and 1–4 query intervals. The complete request stays within 48 KiB; `CMFA1` plus its existing CMCA3 reply stays within 64 KiB. The underlying JSON/string and original-credit limits are unchanged.

The worker binds itself to the creating parent's owned Windows job / Linux parent-death signal **before** storage access. It then calls `executeExistingCreditQuery`:

- Both the database and recovery sidecar must already be regular files. Missing/orphan paths do not create directories, databases or recovery journals.
- `CreditLedgerOpen::ExistingQuery` uses SQLite `READONLY`, checks the existing WAL/schema/binding/audit and budget, and cannot call `apply`. It does not set writer journal mode, page-count limits or other writer persistence configuration.
- The query-only recovery journal cannot prepare or settle. Inspecting Prepared or divergent recovery cannot become forecast authority or an implicit retry.
- Normal coordination leases and SQLite reader coordination can still touch lock/shared-memory files. This is a logical read contract under a protected cooperative directory, not byte-for-byte filesystem immobility or hostile-path authentication.
- Legacy v1 source streams are not migrated/adopted by this adapter. The existing writer/helper defaults keep their prior semantics; the new strict helper has no mutation operation.

The helper uses the existing minimal environment, anonymous bounded IPC and cancellation/reaping transport. An unexpected executable/protocol fails rather than falling back to the ordinary mutating helper. This batch does not add normal-runtime helper discovery/installation or establish hard spawn/kernel/D-state deadlines.

SQLite's [open interface](https://www.sqlite.org/capi3ref.html#sqlite3_open) specifies that READONLY errors for an absent database. Its [WAL documentation](https://www.sqlite.org/wal.html#read_only_databases) explains the read-only WAL support added in 3.22.0 and the possible need for readable/existing or creatable `-wal`/`-shm` files. The existing optional system ABI minimum remains 3.31. This adapter does not use an immutable-URI shortcut, change WAL mode or lower the source writer's page limit to obtain a read.

## One coherent read cycle

`IncomeForecastOwner::start` is single-use and starts an idle background owner without reading a store. An explicit bounded `ForecastScenario` supplies the report clock/time, qualified work-period definitions, remaining schedule, quotes and optional cost scenario; it supplies **no credit totals or committed sequence**.

A read cycle requests today, rolling-24h, cumulative and up to 64 calibration periods, grouped into at most 17 helper calls of at most four queries each. The report cut remains fixed. Each validated reply must have a committed cursor and a readable settled journal state. All chunks must match sequence, snapshot ID, next cursor, source-as-of and observation time. A source change between chunks discards the whole candidate; no partial collection of query replies is published as one complete report.

Only those committed replies populate `ForecastCredits`. Queue tickets, helper tickets, partial progress and Prepared targets never supply source identity or income. Snapshot IDs use the existing ledger's bounded identifier grammar; `fixture.*` remains required for model/work qualification keys, not for every otherwise valid fixture snapshot ID. The owner's accepted high-water mark refuses rollback or contradictory identity at the same sequence within that instance. It is not a cross-parent rollback registry or proof against malicious same-privilege replacement.

An explicitly observed source/profile change calls `invalidate`: cached calculations and any current read are cancelled immediately, and a fresh explicit scenario submission is required. There is no filesystem watcher or automatic source polling/retry. Source changes discovered during a new read are handled by the same coherence check.

## Memory, clocks and publication

There is one queued/in-flight job, no speculative read queue. Admission validates sizes before copying: 64 calibration periods, 4096 calibration work intervals in total, 4096 future intervals, 4096 power intervals and 4096 tariff-rule rows in total; identity fields retain bounded sizes even when the optional electricity input is disabled. This tighter job bound does not enlarge any public IPC or standalone tariff capability. Caller-held old snapshots are immutable and are not an unbounded owner cache.

Policy bounds are explicit and independent:

- projection refresh 100–60000 ms, default 1000;
- per-helper timeout 50–10000 ms, default 2000;
- whole read-cycle budget 500–60000 ms, default 10000.

Quote freshness is bounded to 1 ms–24h, with cross-feed skew no larger than that age limit. The existing quote validator still checks both observation clocks; no event-time or provider qualification is added.

The total cycle deadline uses a real steady-clock budget and bounds each remaining helper flight. A failed read does not automatically retry. Unreaped transport latches a terminal fault.

The background clock watch checks monotonic/UTC reversal and deviation greater than 2000 ms from the explicit parent's initial mapping. Clock sampling and comparison share the owner lock so concurrent quote admission cannot make an earlier background sample appear to reverse time. A fault latches, revokes derived output and refuses later admissions; changing a saved setting or fixing the clock cannot revive that instance. The injectable clock is a pure nonblocking test seam, not a place to perform disk/provider calls.

Successful inputs may be revalued in memory on the independent projection clock without querying the ledger or refreshing credit/report timestamps. Expiry and day rollover suppress derived values, including a value that expired during its calculation. Current quote updates are bounded separate jobs. Tariff FX has an explicit replace/clear option; an ordinary token/USDT update preserves the independently supplied tariff FX. No exchange rate is defaulted to a peg.

Session suppression is frozen. Market suppression ignores supplied quotes/market FX and refuses quote-update admission while retaining actual token facts. Energy-cost suppression removes the power scenario while preserving configured-cost visibility, so net shows unavailable rather than zero or a silently hidden configured row.

All store calls, exact work/tariff calculations and full report formatting occur on the background thread outside the owner mutex. Snapshot/admission/invalidation/`requestStop` are memory-only, including bounded constant unavailable-status labels. Stop-before-start consumes the instance. Stop cancels queued/in-flight reads, revokes the projection and freezes the stopped snapshot; late jobs cannot publish. A preallocated empty terminal snapshot prevents old numeric output from reappearing if replacement allocation fails after revocation; allocation failure is not fault-injected here. Pending means admitted publication work, not proof that a kernel child has been reaped. Joining is separate and belongs after miner drain. Ready means a computation completed; consumers must still check each quantity's coverage/availability and cannot infer mining or optimization permission.

## Supervisor and native tests

The optional `SupervisorOptions.forecastFactory` receives the session's market/energy overrides. It is constructed and started once after explicit intent, with a second stop check after factory construction. Stop revokes intent and the forecast before draining workers; final joins follow worker drain. Child retries do not create another owner. Rendering copies only the already formatted `FinancialView`; public stats serialization receives no credit/model/cursor/path state.

`forecast_supervisor_fixture` uses the ordinary native dashboard and supervisor, a real source-query helper, synthetic setup records and fake loopback workers. It tests full projection, independent suppression, private/default stats separation and Q/controlling-terminal Ctrl+C during a stalled read. Separate lifecycle probes kill only their own synthetic parent, observe the bound read helper terminate and confirm a fresh parent can query unchanged source facts. No fixture stall marker belongs in the ordinary helper/library.

## Executed evidence

- Windows: **65/65 distinct default-clock focused CTest cases** pass across the forecast, existing credit/energy, economics, settings, supervisor and architecture selections. The final clock/bounds amendment passes **2/2** owner/lifecycle cases; the separate offline named-clock build passes **3/3**, including the added DST owner case. Final owner checks are **113 ordinary / 126 named**. The preceding 109-check owner and 10-check lifecycle versions each passed five repeated runs.
- Linux (direct GCC, not Linux CMake): final owner **113/126**, lifecycle **10** and native forecast PTY **27** pass. Own-code/helper **ASan+UBSan** passes ordinary/named owner **113/126** and lifecycle **10**; the external date-tz object is uninstrumented. Named checks cover 23h/25h full-day costs and true civil-midnight invalidation through the real existing-query helper.
- Rebuilt Linux credit cursor/process/owner/journal/review/ledger **419/65/62/133/200/99**, cursor/journal/recovery subprocess **52/27/40**, supervisor/settings/display **257/152/77**, previous settings and credit-review PTY **21/23**, forecast oracle **1500 cases / 9000 comparisons**, and dashboard **18** scenarios pass. Python discovery passes **123 tests with seven explicit fixture skips**. Ordinary artifacts exclude forecast activation and private stall seams.
- Tests cover coherent/mixed/rolled-back snapshots, all 64 periods, missing/orphan/Prepared storage, query-only mutation refusal, preserved originals, protocol/size rejection, background expiry, concurrent clock sampling, in-flight invalidation, timeout/no-retry, bounded stop, native overrides and public-stats privacy.

An initial C++ test declaration/optional-access issue, a test-target list typo and harness expectations were corrected before final validation. Genuine kernel/disk failure, allocation-failure injection, global helper quarantine, real source/work/measurement eligibility, source notification wiring, runtime settings/activation, dependency trust/packaging, production CUDA/SASS/verifier and accrued monetary billing remain unqualified. No installed miner or real device workload was exercised.

```text
cmake --preset tests
cmake --build --preset tests --target test_forecast_owner forecast_supervisor_fixture canary_forecast_query_worker forecast_query_fixture canary_credit_worker test_product_supervisor
ctest --preset tests -R "^(forecast_.*|product_supervisor)$" --output-on-failure
```
