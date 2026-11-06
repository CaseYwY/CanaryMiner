# Public-helper identity and uncertainty (P-06e)

M-MARKET/M-SUPERVISOR; R-MKT-003, R-SAFE-001/002, R-PRIV-002, R-REL-001;
T-MARKET/T-INTENT/T-PRIVACY/T-RELEASE; D-156.

**A local process-ownership repair, not public-source activation or application-wide quarantine.**
Ordinary CLI still installs no public market factory. No account, wallet, market, pool, game or GPU is contacted by the
ownership fixtures, and no protocol/donation path changes.

## Reproduced failures

The old POSIX cleanup sent `SIGKILL` to a numeric process group before checking whether it still owned its child. If another
reaper consumed the child, observation ignored `ECHILD`, cleanup treated it as a successful reap, and the client could start
another helper. A private minimal actual-client fixture reaps only its own spawned synthetic child and suppresses every
numeric signal. It is observed red with two spawns/two stale signal attempts, then green with one spawn/no signal attempts
and two parent-only Unreaped outcomes. No potentially reused process/group receives a real signal during that reproduction.
The retained regression uses linker interposition, not an injectable API compiled into ordinary binaries.

A second actual market-owner regression observes five failures before the owner-level latch: a helper-ownership fault
latched only one feed, allowed another feed/economic request, and permitted a newer generation. Helper ownership is now a
whole-owner terminal fault, including a result delivered during cancellation. Retained original quotes do not become
current reference authority, and a stopped/faulted snapshot does not prove a native helper has exited.

## Identity and admission

- Linux still uses the fixed executable, minimal environment, separate spawn group and close-from file actions. It now
  requires default `SIGCHLD`, no `SA_NOCLDWAIT`, atomic `pidfd_spawn` and `waitid(P_PIDFD)` (Linux kernel capability since
  5.4). Public-helper capability requires glibc **2.40 or later**, rather than the earlier close-from-only 2.34 floor;
  unsupported builds return unavailable without creating a child or linking unavailable pidfd-spawn symbols. The 2.40
  floor excludes the upstream-reported 2.39 failed-exec descriptor leak, not all dependency/security release gates.
- `pidfd_spawn` returns the kernel identity as part of creation. A successful nonblocking child wait verifies waitability
  before any termination signal. A separate `posix_spawn` followed by `pidfd_open` is deliberately **not** used: even
  checking that a later descriptor names a child cannot exclude reuse by a different child in the same application.
  Lost reaping/error outcomes are terminal and are not repaired by opening a later PID, procfs discovery or process scans.
  Subsequent signaling uses only the creation-bound identity, so a competing reaper cannot redirect it to a recycled PID.
- The Linux installed public helper is a trusted single-process worker. Termination targets that process, not an arbitrary
  descendant tree or copied group number. This does not introduce a sandbox or qualify malicious/replaced helper behavior.
  Windows retains the owned process and one-process kill-on-close job; failed binding never resumes an unbound helper.
- Cancellation/module faults are rechecked at native creation and Windows resume boundaries. Native creation, filesystem,
  loader, trust and scheduler calls still have separate hard-bounding/packaging gates; an atomic check is not a realtime
  promise that another thread cannot change state immediately afterward.

## Cleanup participates in the result

The client computes its request result, explicitly settles the native owner, and only then returns. Unconfirmed cleanup
replaces timeout/cancel/parse outcomes with **Unreaped**, an empty body/time/status/retry payload. RAII cleanup after a copied
return value cannot conceal uncertainty. Existing CMJSON1 status numbers and helper ceiling remain unchanged: parent-only
Unreaped is neither encodable nor accepted from a child. Busy/invalid/pre-cancelled requests perform no new launch.
The final publication boundary also checks other public clients' unresolved ownership, not just its own successful reap.
An actual two-client nested native-wait interleaving is observed red with a leaked success/body, then green after this final
check; the other client's unknown owner remains quarantined and no third child starts.

Each admitted native owner reserves one of 64 module-local slots before spawn; the quarantine vector is preallocated there,
not during teardown. A failed bounded settlement retains the owner/handles, blocks new public-helper admission across
replacement clients, and cannot silently discard the reservation at destruction. Later admission may reclaim only an owner
whose actual exit is proven. A terminal lost-ownership error is never re-observed/signaled by this module. Independent
clients already in flight notice the module fault; this is not a shared registry for other helper classes or mining workers.

The existing 250-ms cleanup-attempt bound is a userspace wait, not a guarantee of kernel termination or whole-process exit
latency. No background reaper thread, forced global process scan, driver reset or runtime activation is added.

## Evidence and limits

Linux linker fixtures cover stolen reap before/after the creation call returns, native wait error, deliberately withheld
exit proof, retained-owner destruction/reclamation, cancellation/overflow with unconfirmed cleanup, three incompatible
SIGCHLD policies, unavailable atomic spawn and refused signaling. Real signals in the latter fixtures use only the pidfd returned for the fixture's
own child; numeric kills never forward. These are deterministic syscall-interposition fixtures, not real kernel D-state,
exhaustion, reboot, power-loss, arbitrary descendant or PID-reuse stress qualification. Ordinary Windows/Linux native
process tests retain success/cancel/timeout/overflow/nonzero-exit/environment/descriptor/Unicode-path coverage.

The pure scheduler and background owner test whole-owner/no-other-feed/no-economic/no-new-generation latching, original
snapshot immutability, stopped current valuation and late Unreaped after cancel. The native adapter separately renders its
fault without starting a replacement source. See the [execution ledger](architecture/execution-plan.md) for exact current
commands, platform counts, sanitizer/repetition results and failures; design text alone is not passing evidence.

Primary API sources consulted after Context7 discovery:
[pidfd_open](https://man7.org/linux/man-pages/man2/pidfd_open.2.html),
[wait/waitid](https://man7.org/linux/man-pages/man2/waitpid.2.html), and
[pidfd_send_signal](https://man7.org/linux/man-pages/man2/pidfd_send_signal.2.html).
The [upstream atomic-spawn rationale/API](https://sourceware.org/pipermail/glibc-cvs/2023q3/083002.html),
[2.39 release announcement](https://sourceware.org/pipermail/libc-announce/2024/000038.html) and
[2.40 failed-exec leak correction](https://sourceware.org/pipermail/glibc-bugs/2024-June/057404.html) govern the stricter
  helper floor. No glibc implementation is copied into this repository.

On older/non-glibc Linux builds, process-native CTest fixtures report an explicit capability skip (77), not successful
owned-process qualification; codec checks still execute first. Older runtime/kernel/dependency combinations have not been
qualified by the current modern-glibc fixture runs.

The later [P-14a local-helper batch](local-helper-ownership.md) adds public/local native allocation cleanup and exercises
Windows native wait-fault interposition. Those fixtures do not force actual kernel failures or qualify physical bounds.
Application-wide helper/worker quarantine, genuinely uninterruptible kernel operations,
parent-death/deployment/loader and auxiliary trust egress, qualified provider schema/terms and production sources remain
open. No requirement is promoted to complete by these CPU-only fixtures.
