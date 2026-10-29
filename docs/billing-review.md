# Native complete-original billing review (P-05q)

M-TARIFF/M-ENERGY; R-UI-007/008, R-TAR-002–005, R-ENG-001, R-SAFE-001/002, R-PRIV-002 and R-REL-001; T-TARIFF/T-ENERGY/T-INTENT/T-PRIVACY; D-56. This slice connects the [default-off billing owner](billing-owner.md) to **S → Electricity / Privacy → Billing originals / recovery review** in the existing terminal. Ordinary idle settings and mining sessions have empty access callbacks and show **UNAVAILABLE**. Opening a page reads an injected immutable in-memory snapshot; it does not construct an owner or inspect a store.

## Explicit controls

- **I** reveals/hides private values locally. Reopening defaults to hidden. Hiding clears acknowledgement and discards cached private document text.
- **F** requests one readonly stage/current-money/stage inspection. It does not apply money, resume input, retry or repair. There is no automatic polling.
- **N** reviews strict **CreateNew** of an empty monetary pair. Storage unavailability does not prove absence; the helper may refuse existing files. No measured zero is inferred from empty initialization.
- **C** reviews **Commit** of a fully received original. Partial input cannot be committed. The complete approved binding, manifest, trace and any source receipt are available before acknowledgement.
- **R** reviews exact **Retry** of the current Prepared/Confirmed monetary original. Observation/query times and nested receipts remain frozen.
- **E** reviews **Reconcile** of an Issued Submit stage. It may record a proven settled/unchanged outcome; it does not apply money or adopt ambiguous creation.
- In review: reveal, review, **Space** acknowledges, **Enter/F2** sends once. **Esc** cancels review; a second Esc closes the page. Up/Down/PgUp/PgDn scroll, **T/B** jump to top/end. **Q/Ctrl+C** revoke the borrowed owner and request the normal session stop.

New chunk Submit remains a separate source/compute orchestration step. These controls cannot edit originals, select files, import/export, delete orphan/partial input, enable collection or automatically initialize another cycle. A helper's live checks remain authoritative after the panel's preliminary eligibility checks.

## Review identity and presentation

Every action passes the exact owner generation/revision from the immutable reviewed snapshot. Changed/replaced snapshots cancel review and acknowledgement. Final key handling re-observes the memory snapshot where available; owner admission and preflight still reject races afterward. One locally accepted snapshot cannot send a second action before a new snapshot arrives, even if buffered Enter/review/refresh keys are received together. Queue admission is labelled **not a durable acknowledgement**.

The page distinguishes:

1. **Current read**: the audited middle monetary observation, with the stable stage bracket's limitation visible.
2. **Retained earlier read**: a pre-write/stopped/failed observation, requiring explicit refresh before another action.
3. **Historical stage outcome**: a saved certificate, never a substitute for current monetary availability.
4. **Historical action proof**: a confirmed action result, never silently relabelled a new current read.
5. **Unknown handoff outcome**: exact retained intent, kept independently of all the above.

Unavailable monetary facts remain unavailable, not zero. Provisional amounts are labelled separately from settled totals and never presented as a payable utility bill, actual income or realized profit. Receipt query times, origin/basis, coverage, ancestry and exact amount numerators are visible in the complete document. Decimal rates/watts are distinguished from energy/charge numerators divided by 3,600,000,000.

## Complete, bounded private document

The pure document builder enumerates every field of the approved path/binding/limits, tariff revisions and calendar/dynamic/tier rules; source binding/limits; stage manifest/prefix/certificate; monetary original and committed prefix; full source request/checkpoint/recovery/totals/trace receipt; retained intent; and historical proof. Partial chunk prefixes are labelled raw bytes, not typed financial input. The approved full binding is shown even when only its prefix has been staged; received-byte count makes the distinction explicit.

Strings are quoted byte-preserving ASCII. Literal quotes, backslashes, controls and non-ASCII bytes become `\xHH`; no terminal escapes, line breaks, lossy `?` substitutions or truncated identities can change the displayed meaning. Numeric formatting preserves unsigned 64-bit values. This is a reversible local review representation, not localized Unicode/grapheme/RTL rendering qualification.

The document has a **16-MiB / 131,072-line** budget plus nested structural bounds. An incomplete or failed document cannot acknowledge or send an action. An explicit end marker appears only for a complete document. It is cached per immutable snapshot/reveal state. The renderer caches wrapped-row offsets per width and slices only the visible rows, preserving every ASCII byte including spaces; it does not rewrap/copy a multi-megabyte original on every frame. Fixed title, acknowledgement, status and hotkeys remain outside the scrollable content. Supported compact layout is 32 columns by 12 rows; smaller terminals show a resize/stop hint. End navigation reaches the actual final row, without the older panels' fixed scroll ceiling.

All values remain private by default, including paths, tariff identities/rates, source channels, times, digests, original power and monetary amounts. Revealed values never enter admission/error messages, application logs, exports or configuration. Closing/hiding discards the page's rendered copies; this is not secure memory erasure or terminal scrollback sanitization.

## Integration and lifetime

`BillingReviewAccess` defaults empty and supplies snapshot, inspect/create/commit/retry/reconcile admission and memory-only requestStop callbacks. `billingOwnerReview` is a borrowed adapter: the caller must keep the owner alive through settings/supervisor use and joins it outside input/render paths. No disk, SQLite, source query or helper wait is performed by the callbacks. Existing owner bounds and installed-helper lifecycle apply.

Idle settings take the optional access as a final argument to preserve existing callers. The supervisor supports the same default-empty access and refreshes only the visible panel's in-memory observation. Its pre-start stop, ordinary stop and final cleanup revoke the borrowed owner, including when the billing page is closed. Terminal Q/Ctrl+C and exit do not revive it. Other UI/runtime/manual-market/electricity/tariff drafts still have independent review/save/cancel behavior; billing review does not save them.

## Evidence and remaining work

Tests include default-off routes, private values and escaping, complete/over-budget documents, stale snapshots, explicit actions and duplicate-key suppression, a near-1-MiB tariff, 512-interval traces and maximum source receipts, narrow-terminal end navigation, real owner/helper create/Commit/Retry/Reconcile and independent PTY cancellation/inspection/SIGINT. Supervisor tests verify borrowed-owner revocation without polling or starting a backend.

Executed evidence:

- Windows/MSVC final focused CTest **11/11**. Review **850**, source including maximum-receipt review **492**, energy capture **1392**, credit review **200**, tariff **1071**, settings/runtime/display **155/235/77**, synthetic supervisor **275**, build recipe and architecture checks pass. Ordinary host CLI was rebuilt with default-empty billing access.
- Linux/WSL/direct GCC review/source **850/492**, energy capture/credit review **1392/200**, tariff **1071**, settings/runtime/display **155/235/77** and synthetic supervisor **259** pass. Billing PTY **17** and tariff PTY **16** checks pass, along with existing settings/runtime-settings PTY suites and the auxiliary import guard.
- The new renderer/controller/fixture and extended source tests pass **ASan+UBSan**, using unchanged already-instrumented billing/energy/helper dependencies. Maximum source receipts contain 64 channels, 512 intervals, 128 retained readings and 32 reset windows; all final fields and the complete end marker remain available.
- Strict syntax checking first caught a draft indentation warning and heterogeneous test key-list literals; both were corrected before runtime checks. The first PTY resize check found that a bottom viewport retained its old row offset after narrowing. Rendering now preserves end position across width/height changes; an added renderer regression and final PTY runs pass. The interrupted native wrapper was rerun through its previously unreached settings/supervisor remainder.

Source notifications and bounded compute ownership, opt-in runtime configuration/reporting clock/cycle selection, archive/selected-set changes, partial/orphan management and production admission remain open. These CPU/WSL fixtures do not qualify real tariffs, sensors, credits, storage/kernel stalls, deployed helper trust/packaging or genuine Unicode/RTL terminals. Actual income remains unavailable until a qualified attributable source exists.
