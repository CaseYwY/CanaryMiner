# Isolated monetary creation and bounded input staging (P-05n)

M-TARIFF/M-ENERGY; R-TAR-002–005, R-ENG-001, R-SAFE-001/002, R-PRIV-002 and R-REL-001 remain partial; T-TARIFF/T-ENERGY/T-INTENT/T-PRIVACY; D-53. This adds complete-input transfer and explicit CreateNew/Submit to the [readonly monetary query](billing-query.md) and [reviewed exact retry](billing-retry.md) boundaries.

## Complete originals within unchanged transport budgets

The parent constructs an immutable manifest from the complete approved canonical binding and optional new chunk. Absence of a chunk means explicit creation of an **empty** recovery/ledger pair. Submit additionally binds the reviewed journal revision and audited prior monetary sequence/head. Preparation, source observation, source query and later recovery instants stay distinct. Only the existing synthetic power/tariff admission qualifies.

`CMBIQ1` carries a canonical header of at most 32 KiB plus one raw byte page of at most 48 KiB; `CMBIA1` carries an at-most-8-KiB header plus, only for Inspect, one 48-KiB page. The existing 256-KiB request / 64-KiB response ceilings are unchanged. Generation/ticket and the entire operation identity bind each reply. Pages may split UTF-8/JSON tokens: no partial page is interpreted as a rule or receipt.

A transaction has at most 26 flights: Begin, up to 24 pages, Commit. One client-level lock covers all flights and final proof validation; all share one 50–60,000-ms deadline and cancellation. The parent performs bounded in-memory serialization/proof checks. Filesystem, flush, SQLite and source-receipt replay work runs in the parent-bound native helper. Windows owned jobs and Linux checked-parent/PDEATHSIG apply. No automatic resubmission follows a failed/unknown flight.

## One private staged slot per monetary path

`<ledger>.billing-input` is a `CMBI1` record with separately byte-length-framed manifest, phase/certificate and the exact received payload prefix, followed by a corruption checksum. Binding and chunk retain their 1-MiB / 128-KiB limits. Total record size is bounded to those limits plus 32 KiB + 16 KiB + 128 bytes. The manifest commits both lengths and both complete hashes. Received prefixes are whole 48-KiB pages or the complete payload. Before final acceptance, the helper verifies hashes, canonical codecs, binding eligibility, sequence and original clocks.

Every update takes the stage lease, verifies the exact previous stage revision, then atomically replaces the record. Append is strictly contiguous; old, skipped, repeated or mixed pages refuse. A prior slot is replaceable only when Complete and the caller explicitly supplies that Complete revision. Receiving/Issued slots cannot be overwritten by a fresh Begin. There is no random per-input directory growth: each ledger has one stage, one lock and one exclusive `.tmp` replacement slot. An interrupted/failed temporary write stays preserved and blocks further replacements; it is never automatically adopted or deleted.

Files are regular-file checked; stage reads reject symlinks/reparse points, and POSIX creation requests mode 0600. Windows inherits the selected private directory's ACL. Replacement flushes the file; POSIX also fsyncs the containing directory. The data/checksum requires a protected private directory and trusted executable: hashes are not authentication against a writer controlling both.

## Staging and monetary state are separate

- **Receiving:** an exact immutable prefix; a complete prefix is ready for a separate explicit Commit. No money has been acknowledged and Commit refuses incomplete bytes.
- **Issued:** durable before any monetary create/submit call. A repeated Commit refuses, including after timeout, crash or lost acknowledgement. The stage retains the entire original. This state can coexist with an unchanged monetary pair, Prepared input, a committed-but-unconfirmed input or a settled journal.
- **Complete:** the stage retains its original plus a compact exact outcome certificate. Creation proves Empty without a fabricated zero total. Submission proves Confirmed or a deterministic Rejected original. The parent rebuilds the canonical journal checksum and expected monetary head/state from the full binding/chunk before publishing a snapshot.

Lock order is **stage → journal → ledger**. `submitReviewed` verifies binding and journal revision under the journal lease before opening writable SQLite, then verifies the audited sequence/head under the ledger lease. A changed head, Prepared input or divergence refuses. All ordinary readonly/retry helpers retain their earlier authority. A stage acknowledgement never grants collection, mining, receipt-refresh or retry authority.

## Explicit restart reconciliation

Inspect reads the staged manifest/prefix/certificate without rewriting anything; an optional expected revision freezes paged reads. Caller code must assemble all pages under that revision and both manifest hashes before treating a restarted staged input as fully reviewed. Receiving can resume only with the exact inspected prefix revision and original remaining bytes.

Reconcile does **not** apply a monetary input. Under ordered leases, it audits an existing pair and can close an Issued submission only when it proves the exact original's Confirmed/Rejected journal, or the completely unchanged reviewed prior journal/head. The latter returns an explicit Conflict/no-total certificate. Prepared remains unresolved until the separate reviewed exact-retry helper completes; Reconcile then records its proof. The original preparation instant is never refreshed. A stale/different/corrupt pair stays preserved.

Creation is deliberately stricter: an existing Empty pair has no durable staging-operation identity. A crash between creating that pair and durably recording Complete therefore remains Issued; Reconcile cannot adopt a coincidentally matching Empty pair. A lost response after Complete can be inspected. Orphan creation, abandoned partial inputs and temporary-slot recovery require later explicit native ownership/orphan handling; no delete, repair or silent adoption is supplied here.

## Verification and remaining integration

Focused fixtures exercise actual helper creation/submission, large escaped bindings, Unicode paths, 512 exact intervals, source-offline receipts, deterministic rejection/correction, stage and journal locks, competing helper CAS, stale journal and diverged monetary head, ordered pages, tampering, generation/ticket/operation identity, malformed proof, Busy/cancellation/deadline and wrong helper/parent. Logical batch capacity produces a proven Rejected outcome; a 1-MiB SQLite page cap preserves both Issued staging and Prepared monetary input without an acknowledged total. The maximum-source suite stages its complete 64-channel/512-interval/128-reading/32-window receipt through the actual helper. Independent processes reconstruct stage frames/hashes, interrupt page/Issued/Prepared/pre/post-COMMIT/settlement/replacement boundaries, recover exact source-offline originals and kill only owned synthetic parents. Ordinary artifacts exclude fixture/crash seams.

Executed evidence:

- Windows/MSVC focused CTest **14/14** passes: input/crash **131/183**, extended source/storage **488/32**, retry/crash **32/56**, query/lifecycle **74/20**, pending/crash **97/198**, legacy billing/recovery **90/41** and build/document checks.
- Linux/WSL/direct GCC input/crash **132/191**, source/storage **488/32**, retry/crash **34/56**, query/lifecycle **76/20**, pending/crash **97/206** and legacy billing/recovery **90/41** pass. Own-code/helper/probe **ASan+UBSan** passes the same selection. Existing instrumented energy dependencies are reused. The input count adds a substituted-PID check; the crash count adds POSIX permissions and symlink checks.
- The initial mixed-manifest fixture changed the creation binding digest without its matching seed digest and was rejected in the parent before it could test cross-page mismatch. The fixture now supplies a valid but mismatched pair and checks reply presence; Linux and sanitizer reruns pass. GCC test-only indentation warnings were split structurally, retaining `-Werror`.

This supplies a synchronous background-client boundary. Full-original native review, background recovery/compute ownership, source notification and runtime configuration/cycle/archive/selection integration remain next. Actual sensors/rates, physical disk/kernel faults, production deployment/trust/global reaping, named-timezone/UI qualification and actual-income sources are not certified by CPU/WSL fixtures.

[P-05o](billing-input-query.md) subsequently adds an independent readonly helper that reconstructs complete or partial staged originals from an approved binding/path, including ambiguous temporary-slot presence. Its stored stage certificates remain distinct from current monetary facts.
