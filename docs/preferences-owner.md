# Background private preference owner (P-14q)

M-CONFIG/M-PRIVACY; R-UI-007, R-SAFE-001/002, R-PRIV-001/002; T-CONFIG/T-PRIVACY/T-INTENT; D-174.

This slice adds the parent-side owner for [P-14p](preferences-helper.md). **Ordinary live settings callbacks are not yet bound to it**; building these libraries starts no helper, source, mining or configuration access. Native completion/review and deployment remain separate work.

## Original parent and one-flight ownership

`PreferencesOwner` binds once to an explicit `SessionIntent`/token and native process lifetime before thread creation. The borrowed non-atomic intent is read only by its control-thread APIs; the background thread receives the original native scope and atomic cancellation, never the mutable borrowed intent. Foreign caller scopes cannot start, consume, acknowledge or cancel a healthy newer parent. Stop-before-start and any attempted start are terminal for this owner; no background auto-restart occurs.

Only its owned background thread resolves configured absolute paths, creates/polls/writes/reaps the inherited helper or accesses disk through that helper. Ambiguous `..` components and POSIX double-leading separators refuse before helper creation rather than silently choosing a different target through a possible symlink. This is lexical target restriction, not protection against malicious aliases, writable parent directories or a filesystem sandbox.

Load/Save callbacks validate bounded canonical typed bytes and queue one immutable request. The original admission clock includes encoding/queue delay; a late response cannot refresh its operation budget. A single reply must be consumed before the next admission. Replies must bind exact operation, kind, ticket and canonical typed document; Saved bytes must equal the submitted original. Empty tariff templates can be loaded for editing, not saved as valid declarations. Pure document validation is separately linked; the parent library does not link the helper-side disk service.

`snapshot()` copies only scalars/arrays/shared ownership under a mutex. Original intent/native loss revokes review and signals cancellation without a new clock, file/process wait, callback, join or C++ allocation. This does **not** promise lock-free access, zero OS synchronization, hard scheduling/native bounds or that thread creation cannot stall. `requestStop()` precedes worker drain; `stop()`/destruction joins only after control duties have drained. Actual process settlement remains distinct from current authority and a retained global fault epoch.

## Unknown writes remain private originals

Once the background claims a Save, cancellation/timeout/malformed or lost acknowledgement conservatively retains an alias-owned exact original request, without allocating during revocation. `inFlight` means background ownership, **not** proof that bytes reached the helper or that a file changed. A helper Unknown invalidates review and blocks all writes and unrelated loads. The only allowed recovery is an explicit same-kind Load, consumption/presentation of its current document, then a matching explicit `acknowledgeInspection`. Acknowledgement clears the block, not the historical unknown outcome; it never retries, deletes, repairs, writes or proves physical durability. The UI still has to present complete originals and obtain that acknowledgement before ordinary integration is qualified.

Failure to allocate an admission/document rejects without consuming the ticket, clearing healthy review or escaping a Save callback. Publication allocation failure instead faults an already-issued request and retains a possible write; no speculative Saved state is manufactured. Historical replies and acknowledged unknown originals never become runtime settings authority by themselves.

## Evidence and limits

Linux strict GCC and own-code ASan+UBSan pass **14 scenarios / 260 checks** each: all eight real stores, reply-slot backpressure, current/foreign epochs, stop-before-start, ambiguous targets, main-thread allocation denial, malformed loaded bytes, a genuinely committed Save with lost or wrong-ticket acknowledgement, held Load/Save, zero-allocation current revocation with an issued original, and explicit Unknown inspection/review. A separate executable fixture implements each transport fault; none is linked into the ordinary worker. An actual red test first demonstrated canonical-document allocation escaping Save in both profiles (**1/18** checks failed); catching only that pre-admission failure restores the same review and allows a subsequent explicit Save.

Windows, repetitions and final privacy/cleanup evidence are recorded in the architecture ledger after execution. No real preferences/accounts/installed miner, GPU/game/provider, physical storage/stall, production packaging, ordinary UI latency or complete collection/disclosure/retention/vault qualification is inferred.
