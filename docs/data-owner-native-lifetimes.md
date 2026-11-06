# Original financial, market, detection and safety lifetimes (P-14f)

M-SUPERVISOR/M-LEDGER/M-FORECAST/M-MARKET/M-GAME/M-PRIVACY;
R-SAFE-001/002, R-PRIV-002, R-REL-001; T-INTENT/T-PRIVACY/T-RELEASE; D-162.

**Cooperative memory admission, not qualified production activation or a completed global lifecycle.** This slice
extends [original native scopes](native-process-scopes.md) to BillingOwner, BillingComputeOwner, IncomeForecastOwner,
MarketDataOwner, GameDetectionOwner and SafetyTelemetryOwner. Ordinary factories, sources, helper protocols, consent,
storage schemas, device discovery and mining startup stay unchanged.

## Reproduced boundaries

- The absent-helper caller-scope program originally fails **six of seventeen** checks, with all six fresh-owner controls
  passing. No helper or source exists for that test.
- Six ordinary native controls pass. All six actual owned-thread queue cases fail after a foreign fault has already
  settled; market additionally calls the fetch callback. Three cached market/detection/safety cases also fail revocation.
  A market callback that marks and settles a foreign fault, then returns a valid synthetic quote, still publishes it.
- Real private synthetic stores prove the financial consequence: the coherent missing-store billing preflight still
  allows creation (**2/11 failures**), a real source-backed candidate remains claimable (**3/16**), and a coherent
  posted-credit report remains current and accepts refresh/update/invalidation (**4/13**). Read-only preservation controls
  pass. Forecast assertions use actual posted tokens, not an invented future forecast without a reward model.
- After initial queue/cache fixes, a further clock callback regression fails **4/20** start checks; a current candidate
  claim fails **1/18** checks. Valid callback time does not prove the original native epoch survived the callback. The
  implementation therefore checks again on callback return, before reporting start or transferring a candidate.
- Allocation testing separately reproduces an uncaught forecast-input validation `bad_alloc` in ordinary and instrumented
  builds. A retained validation-only case also aborts against the pre-fix object. Bounded admission now contains that
  exception and returns Invalid without replacing the prior report; callback-return revocation is a separate case.

The Linux-only fixture gates the actual pthread entry and counts successful `pidfd_spawn` creations, rather than assuming
the queue was delayed. Its self-helper dispatches fixed helper mode first, rejects unknown parent modes, binds its owning
parent for local requests and returns fixed synthetic protocols. It never executes a database, process scan, sensor or
HTTP adapter. Separate financial tests use real bounded helpers over newly owned synthetic stores, not real accounts,
tariffs, meters, credits, games or GPUs.

## Original authority and retained history

Each owner binds before its first callback/thread, explicitly installs that original scope in owned background work and
cannot recapture a newer fault epoch. Admission, execution, current publication and snapshot boundaries revoke old
authority. Caller scopes deny stale access without canceling a different healthy current owner. Stop remains terminal for
one-attempt owners; already explicit Stopped state remains distinct from native-fault Unreaped transport.

Billing preserves historical stage/current-money/action proofs and possibly issued private originals, while removing
review/creation/submission permission. Compute preserves historical source-backed candidates, not claim permission.
Forecast removes the current report/cache and its numeric projection. Old immutable snapshots are historical facts,
never refreshed originals, exit proof or recovery permission. Billing uncertainty is conservative: a retained original
does not claim a native child was created, a write committed, or durable reconciliation succeeded.

Detection withdraws current presence/episode authority to Unknown; safety withdraws the current raw reply. Neither
manufactures absence, a safety permit, fresh readings or physical GPU qualification. Actual late native uncertainty and
original requests remain separate diagnostics.

Market snapshot revocation only changes atomic cancellation and returns a masked immutable view; it never mutates the
background-only polling plan/cache. Publication checks after clock/fetch/parse and before commit. Historical quotes can
remain cached, but `running=false` and helper-fault status prevent fresh valuation. The lifecycle mutex serializes explicit
start/stop, not fetch work. Snapshot lock order does not invert the commit/publication path.

Memory cancellation/authority withdrawal precedes fallible projection or snapshot allocation. Preallocated unavailable
views prevent an old Ready/claimable/current report from escaping if replacement allocation fails. These are cooperative
boundaries, not an OS-wide atomic transaction, hard callback bound, new helper identity or actual reaping proof.

## Verification ledger

Initial targeted Linux strict-GCC ordinary and own-code ASan+UBSan/leak-detection runs pass **17/17** selections each:
scope **17** checks, six ordinary/six queued/three cached/one market-callback cases, plus both helper/unknown-mode controls.
Expanded callback/allocation/foreign-caller/stop and full-regression evidence is recorded only after it completes in the
[execution ledger](architecture/execution-plan.md).

- Final Windows/MSVC isolated default-off full CTest passes **333/333** at concurrency four, with every CANARY boolean
  opt-in OFF. All **17** new scope/clock/artifact/financial CTest cases additionally pass five repetitions.
- Final targeted strict-GCC ordinary and own-code **ASan+UBSan with leak detection** each pass **25** native queue/cache/
  callback scenarios, scope **17**, callback-start **20**, **14** real synthetic financial scenarios and both dispatch
  controls. Six revocation allocation cases cover financial and market/detection/safety current authority; the separate
  forecast validation-allocation case preserves the prior report. Native fixtures count rejected allocations explicitly.
- Full Linux Python discovery passes **157 tests / fifteen explicit fixture skips**. Windows' portable selection passes
  **138 / nineteen fixture or platform skips**, explicitly excluding nineteen GCC-dependent tests, not qualifying them.
  The actual external self-helper guard separately executes and passes both assertions.
- The first MSVC build fails on test-local constant capture; moving the fixture constant into namespace scope fixes it
  without changing production behavior or suppressing diagnostics. Neither that failed build nor earlier allocation
  aborts are passing evidence. Full consumer/native regressions and final privacy are tracked in the execution ledger.
- Fresh own ordinary/instrumented **62-unit/53-executable** consumer builds each pass all **25** suites and five separate
  parent-death/artifact lifecycles. All **41** scope/clock/native/financial selections and both dispatch controls additionally
  pass five repetitions per mode. Native fleet/control/CLI/PTY regressions pass; Windows' enabled public/market selection
  passes **7/7** and direct/helper localhost TLS **25/25** each. Exact owned helper paths/modes have zero remaining children.
  Independent next-batch drafts are not part of these claims or this contribution's staged source.

External native libraries remain uninstrumented. Direct GCC is not Linux CMake; Linux API interposition is not Windows
native fault coverage. Enclosing/runtime/other ownership, actual sources/thresholds/identity, cross-parent/DLL coordination,
hard native/loader/storage bounds, physical durability/retention/GPU/game, deployment/activation and release remain open.
This does not complete P-03, P-05 through P-14 or the full roadmap.
