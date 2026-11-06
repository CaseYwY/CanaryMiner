# Shared native uncertainty and original-session fencing (P-14b)

M-SUPERVISOR/M-PRIVACY; R-SAFE-001/002, R-PRIV-002, R-REL-001;
T-INTENT/T-PRIVACY/T-RELEASE; D-158.

**A cooperative shared fence for four statically linked native owners and explicit mining intent, not completed
application-wide lifecycle or production qualification.** No provider/credit/game/GPU source or ordinary TLS/market factory
is activated. The legacy `ChildProcess` and remaining data-owner lifetime/queue integration are separate open work.

## Actual failures before the changes

- After a synthetic local child loses its exit proof, an actual public client still launches and returns a success body:
  the minimal probe observes local Unreaped, public Ok, **two** spawns and **16** body bytes. The old registries are independent.
  The opposite direction, final cross-module publication and a transient fault cleared before publication are separately
  observed red. No external HTTP request is involved; the helper emits a fixed synthetic response.
- Both duplex and controlled-worker owners have an observe-to-signal race: a test consumes only its own child's exit after
  an alive probe, then observes one numeric group signal attempt. Every numeric signal is suppressed. Both retained
  nine-check regressions fail one identity/signaling assertion before atomic pidfd ownership and pass afterward.
- Original `SessionIntent` remains permitted through foreign process uncertainty and clearing. Its memory-only regression
  fails five assertions before the shared original-epoch fence. The actual supervisor separately returns success through
  synthetic transient faults during fleet startup, a control callback and final join; four of eighteen assertions fail.
- A queued native worker can be scheduled after an intervening already-cleared fault. A Linux executable-level thread
  barrier proves no child exists at enqueue, then produces two failures in five checks before original-epoch binding.
  The first draft used linker wrapping alone, which did not intercept shared libstdc++'s thread launch; that draft is not
  evidence of a gated queue. The retained executable-symbol/`RTLD_NEXT` fixture verifies its barrier before injecting a fault.

## Shared state is memory-only

`native_process_admission.h` supplies one inline state shared across the parent's statically linked translation units:
a nonzero original epoch and an unresolved-owner counter. It performs no native call, allocation, discovery, I/O or callback.

1. An owner reserves its bounded module/native retention storage, then admits against the current no-uncertainty epoch.
2. Unconfirmed native settlement increments the shared fault epoch and retains a counted owner exactly once. All four
   owners refuse new creation while any shared owner is unresolved; in-flight operations stop publishing old authority.
3. Only actual exit proof (or proof that creation never happened) settles an owner. Facade destruction is not proof and
   never decrements shared uncertainty. Existing module quarantine retains the actual native handles/pidfds separately.
4. Actual later proof may clear the unresolved count, but cannot roll back the epoch. An operation spanning the fault still
   refuses success, even when another helper's proof was recovered before its final response was copied.

The epoch/count increments saturate rather than wrap into permission. The numerical saturation boundary is structurally
guarded, not reached by the concurrency fixtures. This is cooperative memory admission, not a native OS atomic transaction:
boundary checks do not guarantee scheduler/kernel/loader hard deadlines or instantaneous cancellation of an ongoing OS call.

## Native streams retain creation identity and resources

The duplex and controlled-worker clients now use atomic Linux `pidfd_spawn`, verified `waitid(P_PIDFD)` and pidfd-only
termination under the same explicit **glibc 2.40+ / corresponding kernel pidfd** capability as the
[one-shot clients](local-helper-ownership.md). No later pidfd acquisition, numeric PID/group signal or process scan is used.
Unknown wait ownership remains terminal. The trusted single-process child, not an arbitrary descendant tree, is the target.

Windows retains the actual process plus its bound one-process job, verifies native waitability before termination and
never resumes a failed suspended bind. Thread handles, temporary channels, initialized attribute lists and POSIX duplicated
descriptors/spawn setup have scoped exception owners. The worker's 256 quarantine entries are preallocated before launch,
inside the caught admission boundary. Cleanup never needs to allocate a new unknown-owner slot.

Duplex bytes and controlled receipts/work are withheld after a changed original epoch. Actual own exit proof stays distinct
from external uncertainty and control failure; a foreign fault does not fabricate proof that this child was reaped.
The private wire formats, raw-work framing, work/donation accounting and ordinary factory choices are unchanged.

## Explicit parent and queued-worker lifetime

`SessionIntent` captures the original shared epoch at an explicit start. Unresolved admission refuses; any later epoch
change revokes that token permanently. Clearing the unrelated helper cannot revive it. Explicit stop still wins, and a new
explicit session is possible only after old intent is stopped and unresolved native ownership is actually settled.

The supervisor checks that same original boundary before/after trusted fleet callbacks, optional work and final settlement.
It revokes before further control work and joins only after the existing worker-drain ordering. Late faults remain exit 8;
an already requested stop at entry creates nothing and retains the existing successful-stop result.

`WorkerProcessOwner` freezes its native epoch at public enqueue, not at the later background launch. Changed epochs refuse
pending creation and command admission, mask live receipt authority immediately, and preserve actual done/reaped facts.
This closes its queued-thread race; it does not claim every older queued data-owner operation has received equivalent
lifetime integration. Nor does it repair legacy `ChildProcess` fork/wait/signaling/reader ownership by implication.

## Verification and unrun gates

Retained fixtures cover the four-by-four blocked-admission matrix, twelve cross-type final-publication interleavings,
transient-clear publication, eight lifecycle scenarios for each stream owner, four native setup-allocation variants,
cross-translation-unit/concurrent memory fencing, actual supervisor callbacks and the queued-worker barrier.
Windows exercises native allocation/attribute/wait/exit/bind/resume/cancel and same-type cross-client fault variants.
The fully instrumented native fleet rebuild also catches the older fixed-probe-count game-entry fixture. Its replacement
explicitly triggers the synthetic entry only after receiving the pending Run, checks the actual episode as well as pre-expiry
revocation, and retains the original production lease. See the [pending-Run evidence](pending-run-revocation.md).
The [execution ledger](architecture/execution-plan.md) records completed commands/results; fixtures are not passing evidence
until run, and Linux direct-GCC evidence is not Linux CMake qualification.

No production test hook, DLL patch, `LD_PRELOAD` setting, native trust-store write, real game scan, GPU access or installed
miner mutation is added. External native libraries remain outside own-code sanitizer claims. Genuine kernel/D-state/PID
reuse, malicious helpers/descendants, loader/deployment, legacy/data-owner whole-lifetime integration, real sources and
physical GPU/game/release qualification remain open. The full roadmap is not complete.

Primary APIs: [atomic pidfd spawn rationale](https://sourceware.org/pipermail/glibc-cvs/2023q3/083002.html),
[waitid](https://man7.org/linux/man-pages/man2/waitpid.2.html),
[pidfd signals](https://man7.org/linux/man-pages/man2/pidfd_send_signal.2.html),
[dlsym/RTLD_NEXT](https://man7.org/linux/man-pages/man3/dlsym.3.html) and
[pthread_create](https://man7.org/linux/man-pages/man3/pthread_create.3.html).
The latter two are used by the Linux test barrier only; no external implementation is copied.
