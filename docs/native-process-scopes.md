# Original native task scopes and credit lifetime (P-14d)

M-SUPERVISOR/M-LEDGER/M-PRIVACY; R-SAFE-001/002, R-PRIV-002, R-REL-001;
T-INTENT/T-LEDGER/T-PRIVACY/T-RELEASE; D-160.

**Cooperative memory-only admission, not completed application-wide lifecycle or production qualification.**
This slice integrates the actual credit owner and the existing explicit-session/queued-worker boundaries. Energy,
authorization, held collection, billing, forecast, market, detection/safety and enclosing runtime owners still need their
own original-lifetime integration. No source, income, mining, TLS or native telemetry factory is activated.

## Observed failures and controls

- The actual `CreditLedgerOwner` can queue its initial query before a foreign native fault, then launch after that fault is
  already settled. An executable-level `pthread_create` barrier proves both that the owner thread is held and that no child
  exists before the fault. The corrected retained queued fixture fails **two of seven** checks before the fix.
- An already Ready credit owner continues presenting current authority, accepts new work and can capture the new epoch at
  a higher generation after a transient fault: **four of eleven** checks fail. A normal stop/higher-generation restart in
  an unchanged epoch is the independent **six-check** passing control.
- Actual creation followed by a transient foreign fault must retain the potentially issued original batch, not call it
  rolled back or permit a fresh generation. That fixture initially fails **one of nine** checks. Synthetic helper replies
  are strict unknown/empty facts; they never open the supplied ledger path or represent actual income.
- A stale caller task can create a fresh `SessionIntent`, or borrow a current intent's token. The expanded memory-only
  session regression initially fails **two of eighteen** checks. A separate actual worker-start fixture fails
  **three of five** because the stale caller can capture the new epoch before its owned thread executes.

An initial private credit helper draft assumed the Windows argument count on Linux and fell through into its parent
fixture. Its recursive helper invocations are **discarded evidence**, not a product regression or a passing test. Cleanup
used creation-pinned identities matched to only that exact private executable and fixed helper command; zero remaining
matching children was verified. The binary was removed. The retained helper dispatches its fixed protocol flag before any
parent construction, rejects unknown parent modes, and has a separately executed two-case non-recursion guard. No broad
process-name kill, numeric process-group signal or unrelated process cleanup is used.

## Explicit scope, never an implicit new start

`native_process_admission.h` retains the existing monotonic shared fault epoch and unresolved-owner counter and adds:

- `NativeProcessScope`: noncopyable stack lifetime over a trivial `thread_local` epoch. It saves/restores the caller's
  original scope, including exception unwinding. Nested conflicting scopes become invalid, not a way to borrow a newer
  epoch. A new thread does **not** inherit this automatically; its owner must carry and install the frozen original.
- `NativeProcessLifetime`: binds once at the first explicit owner start, before background queue execution. Even a rejected
  first binding cannot later recapture a fresh epoch. Normal same-epoch stop/start remains possible where the enclosing
  owner already permits it. A settled foreign fault never renews an older facade.
- Effective native admission: low-level owners use the scoped epoch when one exists, otherwise the current global epoch.
  Scope compatibility remains required at subsequent permit checks. All five native owner types are exercised with both
  stale-scope no-creation refusal and actual fresh-scope CPU-child creation as a positive control.

These primitives do no allocation, callback, native call, process discovery, I/O, joining or automatic thread creation.
A scope is not a counted native owner; entering/leaving it is not exit proof. Cross-parent/DLL coordination, malicious
callers, arbitrary descendants, sandboxing and hard scheduler/kernel/loader deadlines are not supplied.

## Credit owner boundary

The first explicit `start` freezes the native lifetime under the existing memory lock; the owned thread enters that same
scope before consuming jobs. Admission, pre-pop work, result publication and snapshot reads check the original lifetime.
Invalidation cancels future work, discards queued unacknowledged jobs and withdraws current Ready authority without I/O or
joining. Old immutable replies remain **historical**, not renewed current financial facts.

An in-flight batch is conservatively retained until the actual transaction result proves it was not issued. This
speculative retention cannot replace an older uncertain original. A possibly issued write survives stop and failed
restart; neither an epoch change nor facade destruction proves rollback. An allocation failure while publishing the
fault view returns a preallocated unavailable view rather than an older Ready view. Explicit durable reconstruction in a
new qualified owner still follows the existing credit journal/review contract; no automatic retry is added.

## Existing session and worker boundaries

Explicit `SessionIntent` start/permission also honors the effective caller scope. A stale task cannot create fresh intent
or use a token from a newer task, while the healthy current caller remains unaffected outside that scope.

`WorkerProcessOwner` freezes the effective epoch at enqueue and installs it in its owned thread. Stale-scope command,
snapshot-authority and raw-work access refuse; a rejected foreign caller does not consume another healthy task's pending
raw originals. Actual done/reaped facts remain distinct from control revocation and are never fabricated.

## Executed verification and remaining gates

- Windows/MSVC isolated default-off full CTest passes **314/314** at concurrency four, with all CANARY boolean opt-ins OFF.
  The new cross-platform memory scope suite passes **38** checks. Linux-only native interpositions are not claimed as
  Windows native coverage.
- Linux strict direct-GCC ordinary and own-code **ASan+UBSan with leak detection** each freshly rebuild sixteen ownership
  support units and pass memory scope **38**, five scoped native-owner variants **9/7/9/9/12**, session **18**, actual
  supervisor **18**, queued worker/scoped-start **5/5**, all 29 cross-owner cases, sixteen stream fault scenarios and five
  allocation variants (**13** each). Existing native process/identity/duplex and local/public lifecycle selections pass.
- Matching freshly rebuilt 62-unit/53-executable consumer selections pass all 25 suites in each Linux mode, including
  credit **62**, cursor **419**, journal **133**, and the separate parent-death/artifact lifecycles. All four retained credit
  scope cases pass **7/11/6/9** checks and **five** complete repetitions in each mode; the helper guard passes **2/2**.
- Twelve affected native suites and seven own core units rebuild/pass in both Linux modes. Language separately rebuilds
  its seven core units and passes **6157**, language **56 CLI / 14 PTY**, tariff **16 PTY**, energy **24 PTY**, manual
  **20 PTY**, pool **14 CLI / 9 PTY**, game **12 PTY** and runtime/base PTYs.
- Final-source Windows **55** native-fault/scope/session/supervisor/ordinary/artifact cases pass five repetitions. Linux
  **61** shared/scope/native scenarios and the separate **18** ordinary-child scenarios/two startup trace tests each pass
  five complete repetitions in ordinary and instrumented modes. Fresh matching native fleet, raw-work and retained TLS
  regressions pass both modes; separate Windows enabled native TLS passes **12/12**. Enabled public HTTPS passes Windows
  **7/7**, Linux **103/53** contract/process checks, and **25 direct + 25 helper** localhost cases per platform/mode.
- Full Linux Python discovery passes **147 tests / eleven explicit external-fixture skips**. The initial full discovery
  is not passing evidence: Windows reports one overbroad NVML scanner failure and five errors (absent GCC/default-codepage
  source reads), and Linux separately reproduces the scanner failure. A complete-function-name boundary fixes the
  false positive without allowing either memory/graphics frequency enumeration; its new classifier regression is red
  before the fix and green afterward. Explicit UTF-8 source reads pass the seven affected source tests on Windows;
  both platforms pass the two scanner tests. Windows' GCC-dependent discovery classes are not called qualified or
  converted into passing skips. Existing Windows native CTest evidence is independent.
  The separate Windows portable Python selection passes **128 tests / fifteen fixture or platform skips** and explicitly
  excludes **nineteen** GCC-required tests; this is not a full Windows Python discovery qualification.

Full commands, additional regression/repetition results and privacy counts belong in the
[execution ledger](architecture/execution-plan.md). External SQLite/OpenSSL/native libraries are uninstrumented; Linux
CMake is unavailable and is not inferred from direct GCC. Ordinary-source/artifact guards do not certify runtime behavior.
Standalone fixture Python tests explicitly skip when discovery has supplied no native fixture; their actual integration
drivers execute the assertions separately. The previously unguarded supervisor discovery import produced two errors and
was corrected, not reported as a passing discovery run.

The [official NVML device-query reference](https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlDeviceQueries.html)
distinguishes the supported event-reasons bitmask from supported memory/graphics frequency lists. The discovery correction
changes only the scanner, not native symbols, GPU access, safety policy or driver behavior.

Other data-owner original lifetimes, older Linux runtime compatibility, native hard bounds, actual sources/terms, helper
deployment, physical storage, GPU/game, terminal/human and release qualification remain open. This slice completes none of
those gates by implication and does not complete the roadmap.
