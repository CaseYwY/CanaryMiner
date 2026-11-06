# One-shot and held local-helper ownership (P-14a)

M-SUPERVISOR/M-PRIVACY; R-SAFE-001/002, R-PRIV-002, R-REL-001;
T-INTENT/T-PRIVACY/T-RELEASE; D-157.

**A shared local-transport repair, not application-wide quarantine or production activation.**
Credit, energy, authorization, held collection, billing, forecast, detection, safety, identity and one-shot TLS inspection
clients already use `LocalTaskProcess`. This change preserves their request protocols and default-off admission. It adds
no provider call, store adoption/replay, GPU discovery, game observation, mining or ordinary TLS/public-market factory.

## Reproduced failures

A private minimal actual-client fixture consumes only its own synthetic child's exit proof and suppresses all numeric
signals. Before the fix it observes two launches and two Unavailable outcomes; afterward it observes one launch, no numeric
signals and two Unreaped outcomes. The retained Linux `run-stolen` regression is observed red with eight failures in eleven
checks before the implementation and passes afterward. An unavailable response must not conceal unknown process ownership.

Native allocation fixtures separately expose resource leaks in the public client: Windows failures after pipe creation
produce three failures in eighteen checks, and failures after attribute initialization produce six in eighteen. The POSIX
fixture fails six of thirteen checks after initialized file actions/attributes. Those fixtures subsequently pass with
scoped resource cleanup. The POSIX allocation fixture cannot launch a child even if its injection misses; the Windows
allocation fixture checks that no child was created. These are allocation/interposition tests, not physical exhaustion.

## Creation-bound identity and retained uncertainty

- Linux requires default `SIGCHLD`, no `SA_NOCLDWAIT`, atomic `pidfd_spawn`, `waitid(P_PIDFD)` and pidfd-only termination.
  The helper capability requires glibc **2.40+** and the corresponding kernel pidfd facilities (full child-wait support
  since Linux 5.4). Older builds fail unavailable without spawning/linking the unsupported API. The stricter glibc floor
  excludes the documented 2.39 failed-exec fd leak. No numeric PID/group, later `pidfd_open`, procfs search or replacement
  identity is used for cleanup. The trusted helper is single-process; this is not arbitrary-descendant containment.
- Windows retains the native process and one-process kill-on-close job. A failed suspended job bind never resumes an
  unbound child. Cancellation and module faults are checked again at creation/resume boundaries. Native process/thread
  handles, mappings, pipes and initialized attribute lists have explicit scoped owners even on allocation exceptions.
- A successful nonblocking wait verifies ownership before termination. A wait/exit-query error becomes terminal unknown:
  that owner is never re-probed or signaled. Deliberately withheld exit proof remains retained and can be reclaimed only
  after a later actual proof, not merely after a cancellation, callback or destructor.
- Each admitted operation reserves one of 64 module-local slots and preallocates quarantine storage before native launch.
  Unconfirmed cleanup retains the owner across client destruction and blocks other local clients. Owners already in flight
  check the same module fault, including immediately before publishing a successful response or accepting readiness.

`run` and `hold` settle before returning rather than relying on a destructor after copying their result. Unconfirmed
cleanup wins over cancellation, timeout, malformed/oversized output and a thrown readiness callback. Every such response is
empty. `childStarted` records **actual native creation**, including suspended bind/resume failures and cancellation after
creation; a refused replacement has `childStarted == false`. It is not successful startup, readiness or proof of reaping.
Held readiness is rejected after exit/unknown ownership; an accepted callback does not turn later lost ownership into success.

Windows and POSIX resource RAII also covers the public helper. Its CMJSON1 ceiling and parent-only Unreaped status remain
unchanged. The [public-owner contract](public-helper-ownership.md) still applies.

## Checks and explicit limits

Retained tests are `test_local_task_ownership.cpp` (thirteen Linux lifecycle scenarios),
`test_native_helper_faults.cpp` (nineteen Windows public/local cases),
`test_posix_helper_allocation.cpp` (two Linux transport variants) and `test_local_helper_artifacts.py`.
They exercise real owned synthetic child creation with test-only native-call interposition; no production injection API or
DLL patching is supplied. Cleanup touches only handles/pidfds returned by the fixture's own creation. Ordinary binaries
keep their existing success/cancel/timeout/overflow/parent-death/Unicode-path and higher-level recovery tests.
See the [execution ledger](architecture/execution-plan.md) for exact platform, sanitizer, repetition and consumer evidence.

The 250-ms cleanup attempt is not a kernel/driver/process-exit hard guarantee. Native API/loader/filesystem stalls,
real PID reuse, uninterruptible kernels, malicious/replaced helpers, arbitrary descendant trees and physical durability are
not qualified by interposition. Windows native wait-fault fixtures are now exercised; they do not force actual kernel faults.
The public/local/duplex/worker registries remain separate. Application-wide uncertainty admission, the remaining native
owners, deployment, actual sources/safety/telemetry and GPU/game/release qualification remain open. No requirement is marked
complete by this slice.

API sources: [waitid](https://man7.org/linux/man-pages/man2/waitpid.2.html),
[pidfd signals](https://man7.org/linux/man-pages/man2/pidfd_send_signal.2.html),
[atomic glibc spawn rationale](https://sourceware.org/pipermail/glibc-cvs/2023q3/083002.html),
[glibc 2.40 leak correction](https://sourceware.org/pipermail/glibc-bugs/2024-June/057404.html),
[CreateProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw),
[WaitForSingleObject](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject) and
[CloseHandle](https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-closehandle).
No external implementation is copied.
