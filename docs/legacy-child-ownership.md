# Ordinary supervisor child ownership (P-14c)

M-SUPERVISOR/M-PRIVACY; R-SAFE-001/002, R-PRIV-002, R-REL-001; T-INTENT/T-PRIVACY/T-RELEASE; D-159.

**Creation-bound ordinary child ownership, not completed application-wide lifecycle or production qualification.**
The ordinary child keeps its command line, stdin-close drain, output framing and console membership. No controlled-worker
protocol, source, GPU activity, ordinary TLS/market factory or production binary is enabled.

## Failures and identity

The former header-only ChildProcess uses fork, numeric wait/signal, unqualified Windows waits and a blocking reader.
Actual self-owned CPU children have their exit consumed at lost-reap and observe-to-signal boundaries. Both private minimal
probes fail **5/10** checks; both retained probes fail **7/13** before replacement. They observe numeric signal attempts,
late probes and new children without shared uncertainty. Every numeric signal is suppressed; no unrelated process is touched.

`legacy_child_process.h` stays header-only to preserve native/CUDA link recipes, but separates native ownership from the
facade. A fixed 256-entry module quarantine is reserved before creation; teardown allocates no unknown-owner slot. The same
[shared epoch](shared-native-admission.md) fences all five statically linked native owner types. Destruction is not exit proof.
Lost native wait ownership is terminal and retains its pinned identity; merely pending waits require actual later exit proof.

## Native, stop and reader lifetime

- Linux uses atomic pidfd_spawn, waitid(P_PIDFD) and pidfd-only termination, never later pidfd acquisition or numeric PID/group
  signals. Ordinary-child capability is explicitly **glibc 2.40+ / corresponding kernel pidfd support** and default waitable
  SIGCHLD. Older runtime/distro compatibility, clean-machine deployment and Linux CMake remain **unqualified**; no unsafe
  numeric fallback exists. Current-host direct GCC does not qualify older runtimes.
- Temporary/duplicated FDs, POSIX spawn setup, Windows pipes/thread handles and initialized startup attributes have scoped
  cleanup. Windows creates suspended, binds an owned one-process kill-on-close job and checks original admission/stop before
  resume. Failed setup/resume/reader construction distinguishes actual creation from successful startup, settling before
  return when proof exists and retaining native ownership otherwise.
- The supervisor supplies its original explicit-session/signal-stop predicate at creation/resume/final-start boundaries.
  Windows uses proper wide quoting and UTF-8 self-path discovery from the wide OS path. Bounded paths/arguments refuse NULs.
  Inherited console/POSIX group, stdin-close stop, ordinary drain/retry budgets, donation and full-work-batch rules remain.
- Cooperative nonblocking output drains available tails after known exit without requiring uncertain peer EOF. The reader
  captures retained native ownership, not a destroyed facade. Exceptions fault the shared epoch while exit proof stays
  distinct. A cleared foreign fault cannot revive old-session intent or later old-child output.

These are not hard native/callback deadlines. Genuine kernel/driver stalls, blocked sinks, malicious descendants, cross-parent
coordination and physical GPU/console behavior remain separate qualification.

## Verification and open work

Standalone CPU tests cover stdin/CRLF/stderr/partial tails, long-line bounds, empty/quoted/backslash/Unicode arguments, Unicode
executable paths, path/argument/stop refusal, inherited POSIX group, unrelated-FD exclusion, transient output withdrawal and
throwing-sink containment. Native Linux fixtures cover lost/racing/error/pending ownership, quarantine/foreign admission,
unsupported spawn/SIGCHLD, exit, before/after-creation stop, live-peer reader cancellation and actual reader-thread failure.
Executable pthread interposition is test-only, not a production hook or LD_PRELOAD setting. Windows faults cover allocation
before/after initialized attributes and after resume, wait/exit faults, pending recovery, bind/resume/creation-cancel and
cross-client faults, including console-flag assertions. Actual ordinary-supervisor fixtures use only CPU children, unavailable
telemetry and no stats server. Completed runs and failures are in the [execution ledger](architecture/execution-plan.md).

Creation-time cancellation or original-epoch revocation is not a retryable runtime error. A separate actual-supervisor trace
regression initially fails both cases because onExit labels them as ten-second retries. Startup failure now distinguishes
intentional stop with actual native settlement from a latched original-session fault; it does not fabricate exit zero or
consume a retry slot for either outcome. Genuine creation failures with intact intent retain the existing retry policy.

Data-owner original lifetimes/queues, ordinary feature activation, sources, hardware, trust/egress, deployment, physical
durability, long-run/release and the full roadmap remain open.

Primary APIs: [atomic pidfd spawn](https://sourceware.org/pipermail/glibc-cvs/2023q3/083002.html),
[spawn signal/group inheritance](https://man7.org/linux/man-pages/man3/posix_spawn.3.html),
[waitid](https://man7.org/linux/man-pages/man2/waitpid.2.html),
[Windows console inheritance](https://learn.microsoft.com/en-us/windows/console/creation-of-a-console),
[nonblocking anonymous pipe state](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-setnamedpipehandlestate),
[CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw).
