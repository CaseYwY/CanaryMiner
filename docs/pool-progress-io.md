# TLS-ready pool progress and conservative send evidence (P-12a)

M-TLS/M-SUPERVISOR; R-TLS-003/004 and R-SAFE-001/002; T-TLS/T-INTENT/T-RELEASE; D-92.

**This is the protocol-side progress contract, not an implemented or enabled TLS backend.** Existing ordinary TCP transports retain their prior path. SSL URLs, secure presets, backend trust/hostname verification, cancellation/connection ownership, fallback consent and donation TLS still require subsequent implementation and qualification. Nothing here claims a pool or fixture is encrypted.

The [backend study](research/secure-transport-backends.md) identifies two incompatible socket assumptions: TLS reads may require writes and writes may require reads, and internal plaintext/control progress can remain while a socket is quiet. It also explains why a TLS write reporting zero newly accepted plaintext can already have emitted ciphertext. Those requirements are now represented and exercised through the **actual canonical `PearlPoolSource` and `PearlRunSession`**, rather than an unrelated model.

## Explicit opt-in, original bytes

`PearlPoolTransport::progressIO()` defaults to false. A true implementation promises bounded, nonblocking `receive`/`send` steps even before a readiness wait. It returns one of:

* `Bytes`: positive exact application-byte consumption/production, within the supplied buffer. A successful send chunk must also have flushed its owned encrypted output; it is not a peer acknowledgement.
* `WouldBlock`: zero application bytes and a required `Read`, `Write` or `ReadWrite` interest. The source waits for that **actual interest**, not the name of its operation. A backend needing both directions must implement a genuine joint wait; the legacy adapter does not guess one.
* `Progress`: bounded internal/control progress with zero application bytes and no wait interest. This is not EOF or application acceptance.
* `EndOfStream`/`Error`: terminal results without application consumption. A future TLS adapter must map authenticated clean closure separately from truncated/unauthenticated EOF; this slice supplies no TLS record parser.

The caller retains the identical pending write pointer/content/length across retries until positive consumption. Positive partial consumption alone advances the offset; no body/newline is regenerated while an operation is pending. Ordinary authorize, gzip/legacy submit, proof/job binding, pool correlation and donation accounting bytes are unchanged. The new result fields and virtual methods are **local C++ contracts**, not a Stratum wire revision.

No opt-in path toggles the underlying socket into blocking mode. The old `blockingRestored` send-evidence flag is trivially true because no transition occurred, not evidence of a physical blocking socket. Legacy TCP still uses and checks its original actual mode transitions.

## Buffered intake and fixed budgets

Read operations attempt a nonblocking step before waiting, including when the TLS library already has plaintext. `bufferedRead()` includes pending plaintext **and** internal read/control progress; work-drain entry points therefore cannot hide those behind a quiet socket. Malformed results (zero/over-limit Bytes, bytes on retry, absent/unknown interest, or contradictory Progress interest) fail closed.

Before a proof is sent, the progress path first drains pending complete pushes through the ordinary canonical job logic. It keeps the existing 64-line / 64-KiB byte ceilings and a 16-step zero-wait progress ceiling. A partial line, an undrained internal record, cross-direction progress that cannot finish now, EOF/error or exhausted budget refuses the proof before sending/reserving a wire request ID. A clean quiet `Read` retry with no pending internal/application input ends intake; it does not claim immunity to a later cross-socket replacement race.

Read/write loops retain the caller's single original monotonic deadline; TLS fragments, control records and retries cannot renew it. The step's own elapsed time is deducted before each readiness wait; an already expired step cannot begin another wait. At most 64 consecutive steps without application consumption are allowed, including always-ready retry loops. Nonblocking reply turns are bounded to 16 steps and retain exact partial framing for a later turn. Existing guard-retirement deadline polling remains active. Backend/allocation exceptions become transport failure, and a write-attempt marker survives an exception. These are bounded CPU/synthetic contracts, not proof that a future native trust/resolver/socket/teardown call is cancellable.

## Unknown is not unsent

For an opted-in progress transport, the source records `transportWriteAttempted` **before** calling send. If that operation never completes, zero accepted plaintext is still `PossiblySent / Unknown`, including backend exceptions or later wait failure. The source closes the connection, publishes the existing correlated terminal and retires the outstanding proof; it does not replay the ambiguous share automatically. A deadline expiring **before any send call** remains definitely NotSent. Legacy TCP's exact zero/partial/full socket-byte evidence remains unchanged.

Locally accepted complete application bytes still need the existing exact correlated pool reply to become accepted. An incomplete uncertain request does not fabricate a known-complete submission or pool acknowledgement; it follows the already distinct possibly-delivered-Unknown/liveness path.

## Verification and limits

The new canonical-source fixture first reproduced **ten failing assertions** before the source changes: opposite-direction writes and reads failed, buffered replacement work was missed, mode toggles occurred and a zero-consumption failed TLS-like write was mislabeled local/unsent. The corrected actual source passes those cases, stable retry-byte comparison, joint interests, internal zero-byte progress, bounded always-ready loops, invalid/throwing steps, unchanged 64-KiB framing, retained fragmented nonblocking turns and progress-mode authorization.

The complete pre-existing fixture suite runs alongside the new cases with fake proofs/devices/clocks/transport. No GPU, real proof verification, certificate, TLS record, public endpoint, pool login, installed miner, fallback, secure preset or production toolchain is qualified. Native Schannel/OpenSSL adapters, structured TLS trust/cancellation/connection state, endpoint-scoped security policy and all real TLS/release gates remain open. Final platform/regression results are recorded in the execution ledger.

Final evidence: Windows/MSVC full serial CTest **185/185**, rebuilt again after the step-time deadline cases. Linux strict direct-GCC ordinary and **ASan+UBSan with leak detection** rebuild and pass seven affected suites: run-session, pool parser, pool source, source observability, observer, progress and gzip. The canonical run-session fixture also passed five Windows and five Linux sanitizer repetitions before the final deadline extension. The final seven-suite rebuild includes that extension. Architecture/import/staged/history privacy and whitespace checks pass. Linux CMake, native TLS/trust, actual pool and production qualification are not claimed.
