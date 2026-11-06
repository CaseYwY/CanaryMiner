# Original display, session-energy and deferred identity lifetimes (P-14g)

M-SUPERVISOR/M-ENERGY/M-PRIVACY; R-SAFE-001/002, R-PRIV-002, R-REL-001;
T-INTENT/T-ENERGY/T-PRIVACY/T-RELEASE; D-163.

**Cooperative software revocation, not qualified hardware sampling, hard exit bounds or complete global ownership.**
This slice extends [original native lifetimes](native-process-scopes.md) through the session-only energy estimator,
background display sampling and the game fleet's deferred identity thread. Defaults, factories, consent, configuration,
source qualification, protocol bytes, donation, helper identities and ordinary mining activation remain unchanged.

## Observed failures and controls

- The actual deferred identity thread is held before entry. After a foreign native fault is already settled, the old
  game fleet launches its identity helper anyway: **one failure in eight checks**. Ordinary native creation and explicit
  owner-stop controls each pass **seven** checks. The regression launches no worker, scans no game and loads no driver.
- The session-energy selection fails **six of seven** scenarios, including stale caller start/restart, retained Running
  state, continued input admission, stale foreign-caller access and stopped-view allocation failure. Startup allocation
  also aborts instead of returning refusal. Same-valid-epoch explicit restart is a passing control, not removed behavior.
- Display sampling fails **five of seven** scenarios. A queued old factory still runs and creates a real synthetic helper;
  a factory or reader can mark/settle a fault, return valid synthetic data and still publish; a stale foreign caller can
  consume a different healthy owner's result. Ordinary and queued-native controls pass. The fixture always includes the
  unavailable-NVML shim, so even accidental native fallback cannot load a real driver.

Self-helpers dispatch their fixed mode before constructing a parent owner, reject unknown modes and never execute native
entropy, database, game, sensor or public HTTP adapters. Linux queue barriers intercept the actual pthread entry and count
successful actual `pidfd_spawn` creation; they do not assume a thread was delayed. These are owned CPU processes, not real
workloads or tests against an installed miner.

## One original background lifetime

The fleet binds before its startup work, scopes nested starts and explicitly carries that original epoch into the identity
thread. The native client cannot borrow a newer cleared epoch. Main-thread start also checks again after thread creation;
an owned thread remains drain/join work, not replacement or recovery permission.

The session estimator binds before model/thread creation, scopes its worker, checks input/current publication boundaries
and withdraws queued observations on native invalidation. `NativeFault` is an appended **in-memory** state, not a new file
format or hardware diagnosis. Known prior totals remain historical; explicit Stopped stays Stopped. A higher generation
can restart only in the same valid bound native epoch. Rejected stale starts cannot recapture a later epoch. A normal
allocation-refused start can be retried explicitly in the same still-valid epoch; no native child was issued by this owner.

Startup contains allocation failures. Cancellation and input withdrawal precede replacement snapshots. A preallocated
empty view masks an old Running snapshot when allocating a stop/fault view fails. Allocation scenarios reject allocations
on **every** thread and assert that the test actually rejected at least one, rather than merely enabling a flag. Old shared
snapshots remain immutable historical facts, not current energy, source freshness or continued input authority.

The sampler binds before its thread, scopes factory/reader work and checks after factory return, around each reading and
before publication. Memory-only request/take/status boundaries clear old jobs/results without closing a native reader or
joining. A stale caller gets no result and cannot consume/cancel a healthy current owner's batch. The default reader also
freezes its own original epoch and checks before local HTTP, after it, before driver open/sample and before returning facts.
Reader construction, native operations and destruction remain on the background thread; cleanup is still required after
revocation. A scope/check cannot preempt an OS or driver call already executing.

## Executed evidence and limits

Targeted Linux strict-GCC ordinary and own-code **ASan+UBSan with leak detection** each pass the identity ordinary/fault/stop
cases, all eight strengthened session-energy cases and seven sampler cases. The latter include exact helper-count and
callback-return controls; both helpers' fixed/unknown-mode controls pass. Four source-artifact checks pass. Full Linux
Python discovery passes **165 tests / nineteen explicit external-fixture skips**; actual native fixtures execute separately.
Whole native/host regressions, repetitions and final privacy are recorded only after completion in the
[execution ledger](architecture/execution-plan.md).

Fresh ordinary/instrumented native fleet rebuilds pass cached-ceiling **12** five times, pending withdrawal **70**, full
owned session **134**, economic session **336** and game runtime **78** per mode. All **18** background cases and four
helper-mode guards pass five additional repetitions per mode; existing display sampling passes **60** checks each. Twelve
native suites and seven own core units rebuild/pass both modes, together with rebuilt Language **6157** and current native
CLI/controlling-terminal regressions. Windows portable Python passes **146 / twenty-three fixture or platform skips**,
explicitly excluding nineteen GCC-required tests rather than qualifying them.

## Validation adjustments, not relaxed production rules

The first Windows full run is **347/348**, not a complete pass: its helper guard incorrectly supplies POSIX stdin to the
Windows mapping protocol. A separate test-only native parent now uses the real inherited request mapping and checks actual
child creation plus the fixed reply. The strict helper still rejects invented arguments; production IPC is unchanged.

Another full run is **347/348** at an older accepted-history fixture. Thirty ordinary repetitions do not reproduce it;
forcing a UTC second boundary between observation and stop does (**one failure in six checks**, with the ordinary full
290-check control passing). The fixture incorrectly assumes the observation timestamp equals the later clean-close time.
It now reopens one second after the stopped owner's actual as-of time and checks **both exact gap endpoints**, all four
unchanged count windows and no invented clock fault. A retained explicit-boundary case uses that same assertion path.
Production history/SQLite/coverage code is not changed; intermittent failed runs are not reported as passes.

The corrected Windows/MSVC isolated default-off full run passes **349/349 CTest** at concurrency four, every CANARY
boolean opt-in OFF. The **19** affected new/background/history/session cases additionally pass five repetitions. Linux
ordinary/instrumented history passes **291** full / **7** explicit-boundary checks, with five further repetitions each;
both fixtures' actual native-parent controls pass five times per mode. Enabled development energy CLI/supervisor rebuilds
include the current session estimator and pass runtime **109**, sibling/layout **22**, and controlling-terminal **42**
per mode. Exact owned background/fleet helper paths/modes have zero remaining children on Linux; the Windows sampler check
also finds zero owned helpers and zero unreadable same-name identities. These are bounded fixture checks, not global
process containment or physical qualification.

Direct GCC is not Linux CMake. Linux pthread/native interposition is not Windows native fault coverage. External native
libraries remain uninstrumented. This does not qualify actual power/threshold/identity sources, real games/experience,
kernel/loader/driver/filesystem hard bounds, physical storage/retention, cross-parent/DLL coordination, remaining
data-owner/enclosing views, production deployment/activation or release. Economic current-view work and the rest of the
roadmap remain separate; P-03/P-08/P-11/P-14 and the full objective are not complete.
