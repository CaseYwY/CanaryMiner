# Default-off background monetary recovery owner (P-05p)

M-TARIFF/M-ENERGY; R-TAR-002–005, R-ENG-001, R-SAFE-001/002, R-PRIV-002 and R-REL-001 remain partial; T-TARIFF/T-ENERGY/T-INTENT/T-PRIVACY; D-55. `BillingOwner` composes the [stage reader](billing-input-query.md), [current monetary reader](billing-query.md), [staged input writer](billing-input.md) and [exact retry](billing-retry.md) on one background thread. Linking the library does not activate it.

## Read and review boundaries

The caller freezes one complete private path/binding/limits and four installed helper paths. The policy defaults to disabled. One explicit `start(generation)` performs readonly inspection; an instance can start only once. Stop-before-start, stop, clock faults and Unreaped cannot be undone by calling start with a newer generation. A new owner reconstructs existing facts, with no automatic retry, create, submit, sampling or mining action.

A read is **stage → current monetary snapshot → stage**. Both stage observations must be valid and agree on presence, complete stage revision and temporary-slot presence. The owner discards a mixed view. This is a stable stage bracket, not a transaction locking both stores for the entire read: the monetary snapshot is the audited middle observation. The stage's saved outcome remains historical even when the monetary pair is absent. Unavailable monetary state never becomes a zero total or evidence of an absent pair.

Every public action requires the current generation and owner revision. The snapshot contains the complete immutable context and separate private stage/monetary originals for subsequent native review. Before **any** write/recovery handoff, the background thread repeats the bracket and compares it with the reviewed view. A changed stage, temporary slot, journal revision, audited head or monetary status prevents issuance and publishes the new reviewable observation where available. The existing helper-side stage CAS / journal-and-head checks still enforce admission against changes after this preflight.

## Explicit actions

- **CreateNew:** explicit empty-pair intent, only with a successfully observed absent stage, no temporary slot, no retained uncertainty and no previously proven monetary head. A Storage result from the monetary reader is *not* classified as proven absence; the strict CreateNew helper must refuse existing/corrupt/orphan stores. A private parent directory must already be readable for the initial stage inspection.
- **Submit:** a new complete chunk, a settled current monetary journal and an absent or Complete stage. The parent validates the exact canonical original before enqueue; the helper binds the reviewed prior revision/sequence/head.
- **Commit:** the already reviewed complete Receiving original. Partial binding/chunk bytes remain preserved and cannot be committed. A Submit manifest must still match the settled prior monetary state; CreateNew retains its separate guard.
- **Retry:** only the current Prepared/Confirmed monetary original. When an older uncertain intent exists, its complete original must match. Retry never replaces or refreshes the receipt.
- **Reconcile:** an Issued Submit stage, checked against the explicit stage revision and current monetary observation. It can record proven settled/unchanged facts, not apply a chunk. Ambiguous creation remains blocked.

A write's exact confirmation is published as `actionProof`, separately from `view`. A pre-write view is not relabelled post-write current state. After a handoff, `reviewable` is false until an explicit refresh. Tickets and queued jobs are never durable checkpoints. Successful exact retry of an uncertain submission still leaves its Issued stage needing explicit Reconcile.

## Bounded admission and lifetime

One borrowed-input admission may allocate at a time; there is at most one queued/in-flight job and no backlog. Before serializing a new chunk, admission bounds the basic trace and every variable-size receipt/checkpoint/recovery field, including the nested batch. It then performs `chunkText` encoding, the 128-KiB cap and a complete `parseChunk` canonical round trip. The validated parsed object becomes the immutable job; malformed receipts cannot pass merely because `boundedChunk` accepted the outer trace. Receipt encoding/copying occurs outside the stop mutex. Final admission rechecks generation/revision after parsing, so cancellation or invalidation revokes it.

All filesystem, SQLite, flush and source-receipt replay I/O stays in installed parent-bound helpers. The admission/stop/snapshot paths perform no filesystem I/O. One background operation has a 500–60,000-ms total steady-clock deadline, including its preflight: at most 11 read flights, 37 for a complete new-input operation, or 12 for exact retry/Commit/Reconcile. Existing 256/64-KiB transport caps, per-view page caps and individual client proof checks remain unchanged. Cancellation is passed through every flight; no failed or unknown operation automatically repeats. Bounded CPU decoding/allocation and OS scheduling are not a hard real-time guarantee.

UTC and monotonic samples are serialized under the mutex. Backward clocks, out-of-range UTC or more than 2 seconds of elapsed-clock disagreement latch ClockFault and revoke admission/publication. A previously observed committed sequence cannot decrease; a different digest at the same sequence or an audited Diverged journal latches Faulted. A stored stage outcome may establish a lower-bound consistency check but never supplies the current monetary total. Unreaped latches even if the result belongs to an invalidated epoch; cancellation/deadline cannot turn it into permission for another helper.

`requestStop` revokes immediately, cancels queued/in-flight jobs and preserves the exact handed-off intent. The terminal snapshot is frozen before join; a late successful reply cannot alter it. `stop` joins the owned background thread. Existing Windows job / Linux checked-parent PDEATHSIG protections terminate owned helpers if their parent dies. Test-injected Unreaped covers the latch, not genuine unreapable kernel I/O or global quarantine.

## Preserved uncertainty

Before helper handoff, the immutable intent retains the exact manifest, original and reviewed monetary state. Failure, cancellation, invalidation or lost acknowledgement retains that intent conservatively. A failed retry cannot overwrite an older uncertain submission. An explicit refresh may resolve uncertainty only with the matching Complete stage certificate, or with the matching Confirmed monetary original for a retry-only intent. This does not imply current-money availability: the newly read monetary status is still separate.

Partial originals, temporary replacements, unrelated originals and Issued creation remain blocked. No abandon/delete/repair/adopt action is supplied. Helpers may have committed durable facts before stop or process death; restart reads them rather than assuming they vanished.

## Verification and follow-up

Focused tests exercise default-off/start/stop, concurrent revision-bound admission, explicit create/submit/rejection/correction, full Receiving Commit, preserved partial/temporary originals, stale review, stage changes around a monetary read, a changed head after preflight, lost create/retry confirmation, offline source receipts, Prepared retry then Reconcile, retained older uncertainty, late-result revocation, clock faults, invalidated reads, deadlines, lower-sequence and same-sequence forks, absent monetary facts and nested receipt bounds. Independent parents verify owned helper death and readonly restart without mutating original files. A separately compiled seam tests Unreaped, including an invalidated flight; ordinary artifacts exclude that seam.

Executed evidence:

- Windows/MSVC focused CTest **19/19**, followed by the final probe/shared-lifecycle/document selection **4/4** (one additional forecast lifecycle case). Owner **139**, Unreaped latch **10** and independent lifecycle **33** checks pass; existing staged-query/input/source/query/retry/pending/storage/build/document regressions pass.
- Linux/WSL/direct GCC owner/latch/lifecycle **139/10/33**, staged query/lifecycle **65/59**, input/crash **132/191** and extended source **489** pass. New own-code/fixture/probe **ASan+UBSan** passes the same selection with the unchanged already-instrumented helper/dependency objects. The shared lifecycle utility's forecast regression passes **10** checks on both platforms.
- Strict GCC caught a test-probe indentation warning; it was split structurally. A Linux input-crash regression then exposed procfs `ESRCH` between opening and reading the just-exited owned task. The shared test identity probe now treats only FileNotFoundError/ProcessLookupError as absence; other failures still propagate. The affected lifecycle/input-crash/source remainder was rerun successfully, including the independent forecast caller. The interrupted wrapper is not counted as a complete pass.

Native complete-original presentation/admission callbacks, readonly source notification/compute orchestration, runtime configuration/cycle/archive selection and production activation remain next. Actual energy/rate/credit adapters, named-timezone/UI qualification, real disk/kernel faults, loader/trust/global reaping and production packaging are not certified by these CPU/WSL fixtures.
