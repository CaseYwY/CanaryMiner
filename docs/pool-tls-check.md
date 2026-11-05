# One-shot isolated DNS/connect/TLS inspection (P-12e)

M-TLS; R-TLS-003/004; T-TLS/T-INTENT/T-PRIVACY/T-RELEASE; D-96.

**Independently default off, development connection inspection only.** `PoolTlsCheckClient` starts one owned native helper for one explicit request. That helper owns platform initialization, synchronous native DNS, candidate sockets, the [shared native TLS stream](pool-schannel.md), verification and destruction. It sends **no application bytes**, accepts no wallet/account/worker/proof fields and closes the connection before returning. A Verified reply is a historical observation, not a reusable socket, mining permission, authentication result for a new connection or automatic retry/fallback authority. The ordinary miner neither links nor calls this client/helper.

`CANARY_ENABLE_POOL_TLS_CHECK=ON` requires an explicitly selected platform TLS engine. The helper is not installed or deployed to the existing miner by this batch. `CANARY_POOL_TLS_CHECK_LOOPBACK_TESTS` is independently OFF and requires explicit private fixture scratch; Windows positive private fixtures also require the existing development private-trust inspector. Configure installs/downloads nothing. Default builds retain only the CPU contract/disabled implementation and owned synthetic lifecycle fixtures, without performing DNS or creating a network socket.

## Frozen request and private local IPC

The request pins one original ASCII DNS host, port and 50–60000-ms total budget. The **same host** is used for resolution, SNI and reference-name verification; no resolved address, peer-provided name or separate display label replaces it. Literal IP/URL/user-info/path syntax is not supported by this initial API. Linux explicit PEM anchors are limited to 64 KiB. Optional Windows development private material retains the same reference, requires empty leaf, disallows simultaneous PEM mode, and permits at most sixteen anchors/intermediates/CRLs per collection, 64 KiB per object and 96 KiB combined supporting material. The queried actual peer remains the inspection leaf. Normal Windows mode still uses automatic validation; private-mode success does not qualify it.

`CMTLSQ1` is a strict bounded length-delimited binary envelope, including the expected ephemeral parent PID. Public DER can contain NUL or newline bytes without truncation or JSON translation. Integers reject signs/leading zeroes/overflow, every blob/count has a limit, and trailing bytes refuse transactionally. It uses the existing local-task request ceiling, not an unbounded provider stream. The entire serialized request is frozen before launch and delivered by the existing read-only anonymous child mapping on Windows or bounded private input channel on Linux, never endpoint/material command arguments or environment variables. The child binds to the owned parent/job before resolver/network work.

`CMTLSA1` contains only status, stage and structured TLS error. It never echoes endpoints, addresses, certificate bytes or native error text. Status/stage/error combinations, negative/out-of-range enums, lengths and trailing bytes are checked before publication. Native TLS failure remains distinct from DNS/connect failure; a chain/name/trust refusal does not become connectivity fallback permission. There is no plaintext path or share replay.

One client object allows **one attempt**, including disabled/cancelled/invalid attempts. Disabled and pre-cancelled requests spawn no child. A late reply after cancellation or the parent deadline cannot be published. This client runs only on an owned background thread; the caller must be joined before destruction. It is not a UI/render/job-intake callback or a new general shell/executable discovery API. Helper paths are explicit trusted local installation inputs under the existing local-task path/lifecycle contract.

## Native connection and verification

Inside the helper, one original monotonic budget spans resolver, all candidate connection attempts and TLS verification. DNS uses native `getaddrinfo`; it is intentionally **not described as natively cancellable**. A blocked resolver, trust operation or destructor remains in the disposable helper, while the parent can stop owning work and request termination/reaping. Native socket ownership is nonblocking throughout, with close-on-exec/noninheritance and RAII cleanup. Read/write/joint waits retain the TLS engine's actual required readiness; each wait is capped at 25 ms and the current remaining absolute budget.

At most sixteen eligible returned-address candidates are started. Pending candidates retain their sockets, with later starts staggered by 250 ms while earlier attempts are quiet; immediately failed candidates do not hold the queue. All candidates share the original deadline, and readiness is confirmed with native `SO_ERROR`. After one succeeds, all losers close **before** the winning socket sends ClientHello. TLS verification failure on that selected connection never causes an address/TLS downgrade/retry. This is a bounded initial multi-address strategy, **not complete RFC Happy Eyeballs, incremental/asynchronous DNS, proxy support or production endpoint qualification**.

The Windows real-loopback regression initially exhausted the entire budget on the first IPv6 candidate despite an available IPv4 server. A separate clean-environment diagnostic measured about 3 ms for localhost resolution, a still-pending first-family attempt and immediate second-family success. The original sequential-all-budget strategy failed repeatedly; the staggered bounded candidates fix the actual call path without extending the deadline or weakening verification. The cancellation fixture was then corrected to wait long enough to reach its asserted handshake stall rather than cancelling before the staggered second address could start.

## Parent lifetime and limits

This batch reuses `LocalTaskProcess` rather than adding blocking parent pipe writes or another process launcher. Its narrow handle/environment inheritance, Windows owned kill-on-close/single-process job, Linux parent-death/process-group controls, bounded nonblocking output, exact exit/tail checks, cancellation and deadline checks remain authoritative. Success requires a valid complete reply **and observed successful helper exit** within the parent's total budget, including launch/IPC/native destruction. Timeout/cancel/malformed/oversize replies supply no success result; an unresolved termination returns Unreaped, not a fabricated exit.

Those mechanisms do not establish a hard bound for OS process creation, kernel/disk stalls or all platform termination behavior. The inherited 250-ms reap attempt is a bounded attempt, not proof an uninterruptible process was reaped. Whole-application/global unresolved-child quarantine, protected helper deployment/servicing, native trust auxiliary egress/caches/enterprise-policy equivalence and clean-machine/OS support still require qualification. One-shot non-reuse is not a substitute for global quarantine across newly created clients. No thread is killed inside a resolver or crypto library; no mining worker, driver, root store or system policy is changed.

## Executed scope

CPU contract tests cover valid/binary-material round trips, every prefix truncation of the basic request, malformed host/port/budgets/material, enum/status coherence, disabled behavior and terminal one-attempt cancellation. Separate synthetic helpers exercise malformed/trailing/overflow replies, clean environment, cancellation of **phase-labelled synthetic stalls**, timeout and actual parent death. Those labelled helpers deliberately sleep; they are not forced stalls inside the real OS resolver/connect/trust APIs. Windows cancellation obtains the actual process handle while the child is still alive, then confirms its exit, rather than mistaking an already-disappeared PID for an observation failure.

Explicit loopback tests use an owned Python/OpenSSL TLS server, fresh synthetic CA/leaf/CRL and **localhost only**, with no application bytes. The no-ticket positive requires both client validation and actual server handshake completion. A separate ordinary-ticket case checks **local** verification only: the inspector's deliberate immediate abort can race the server's TLS 1.3 post-handshake ticket writes, and must not be reported as peer completion or acknowledgement. The first overly strong ticket-case assertion exposed that difference; disabling tickets only in the peer-completion fixture passes repeated runs, while ticket-enabled checks remain independently covered without changing client policy. Linux uses explicit in-memory CA PEM; Windows uses actual-peer-bound strict private material. Both reject that otherwise identical untrusted synthetic peer in normal mode and enforce timeout/cancel against a real connected but silent TLS peer. Normal Windows trust **positive** qualification, IP identity, received private intermediates, trust egress/cache behavior, real/public pools and full persistent transport remain open. No public endpoint, personal identity or GPU is used. The server's immediate local-abort EOF is not claimed as graceful TLS shutdown.

The existing explicit synthetic generator now accepts an optional reference name for localhost fixtures; its default fixture identity and import-without-key-generation behavior remain unchanged. Keys are captured only by the private test process, loaded under private scratch and immediately unlinked. Time-sensitive offline inspector materials must be freshly generated before reusing their future/expired assertions; a previous one-hour-future leaf became valid during the long session and was correctly accepted, so final inspection uses a new unique-issuer set rather than weakening the time check.

Final platform/test results are recorded in the execution ledger. Native Schannel/Crypt32 and external OpenSSL remain uninstrumented. Persistent bidirectional helper IPC, retained socket/stream ownership, actual canonical-source Stratum on Windows, URL/settings/fallback/donation isolation, normal automatic-trust positives and production/GPU/game qualification are not delivered by this one-shot check.

## Lifecycle fixture timing correction

A later default-off full run failed the old combined 250-ms timeout/readiness assertion; its original output did not identify
which conjunct failed. An isolated repeat and 200 bounded four-way replays passed, so they do not establish the original
failure's exact cause. A test-only one-second pre-ready delay then deterministically demonstrates that a valid started-child
Timeout/no-reply result can precede readiness. The production deadline already includes launch and IPC; requiring a marker
inside that short budget was not a valid lifecycle contract. Timeout now checks child-started, Timeout, absent status/stage/
TLS reply and the unchanged 1500-ms observed wall bound, without assuming phase entry.

The first correction's repeat subsequently exposed a second launch-relative assumption: a 500-ms cancellation timer could
fire before readiness, preventing the test from acquiring the live child identity it intended to observe. Adding the same
delayed-startup case reproduces that failure deterministically. The test-only probe now accepts a private cancellation-file
trigger. Python waits for a complete PID marker, obtains the actual process identity while alive, verifies it, then triggers
cancellation. All three original labelled phases plus delayed startup still require Cancelled, child-started, no reply, the
unchanged 2000-ms observed wall bound and observed child exit. Startup additionally proves at least one second elapsed before
cancellation. The independent total budget remains 2500 ms; the private trigger does not reset or extend it.

The monitor wakes and joins on completion or exception, including success/timeout with an absent trigger. The numeric-delay
test interface remains available for real loopback handshake cancellation. Both seams and the startup host remain excluded
from parent/native libraries. Only fixture, probe, assertions and this test-scope documentation change; production deadline,
cancellation/reap, trust, fallback and ordinary activation do not. These are controlled owned-process observations, not hard
OS/kernel termination bounds, actual resolver/trust stalls, global quarantine or physical/production qualification.

Correction evidence: final Windows/MSVC default-off full **241/241 CTest** passes both serial and at concurrency four on the
working tree containing the separately pending P-13ap notice test. An earlier serial wrapper stopped after 122 completed cases
and is not counted as a complete run. The corrected lifecycle passes **20** serial repetitions and **80** full lifecycle runs
with at most four concurrent probes; Windows enabled lifecycle and five-scenario real localhost TLS each pass **five** more
repetitions. Linux strict-GCC ordinary and own-code/helper **ASan+UBSan with leak detection** rebuild/pass **1304** enabled
contracts, corrected lifecycle and five-scenario real localhost TLS. External TLS libraries remain uninstrumented. Architecture,
default-off artifact/import, whitespace and privacy checks pass; the four correction blobs are scanned independently from
the pending notice batch. None of these synthetic/process/loopback results establishes production trust, physical termination,
global quarantine or permission to alter an installed miner.

Final evidence: Windows/MSVC default-off full serial **189/189 CTest**, then final rebuilt focused **7/7** after negative-enum/import-guard additions. Windows/Linux disabled contract suites pass **1305** checks; enabled contracts pass **1304** (the default-off capability refusal is intentionally skipped). The separate Windows enabled selection passes **6/6**, including the new native loopback checks, P-12d 18-scenario MemoryBIO regression and refreshed P-12c 43-check native inspection; it is not the entire enabled tree. The owned-process lifecycle suite passes five extra repetitions, and the **final five-scenario loopback suite** passes five extra complete repetitions after the ticket/peer-completion distinction.

Linux strict-GCC ordinary and own-code/helper **ASan+UBSan with leak detection** rebuild/pass the final **1304** enabled contracts, owned lifecycle and five real loopback scenarios. Killing a timed-out helper does not instrument or certify native OS/library teardown. Six default-off option checks, native-network/TLS-symbol exclusion in the disabled connector, TLS-import exclusion in the parent and no ordinary worker activation pass. Final architecture/import/generator-import, staged/source/history privacy and whitespace checks pass. Earlier fixture count/late-PID observation mistakes and the native helper's NOMINMAX build omission were corrected without relaxing trust. The expired future-leaf fixture was regenerated rather than changing native validity policy. No public endpoint, user identity, root installation, miner deployment, GPU/driver or production qualification is claimed.
