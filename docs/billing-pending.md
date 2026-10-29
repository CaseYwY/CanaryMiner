# Durable monetary originals and explicit recovery (P-05k)

M-TARIFF/M-ENERGY; R-TAR-002–005, R-ENG-001, R-SAFE-001/002, R-PRIV-002 and R-REL-001 remain partial; T-TARIFF/T-ENERGY/T-INTENT/T-PRIVACY; D-50. This extends [source-bound monetary receipts](billing-source.md) with a synchronous helper-side `BillingRecoveryStore`. It supplies durable pending input for both standalone synthetic v1 and source-bound synthetic v2 monetary ledgers.

## One ordered owner of both stores

The store acquires `<ledger>.billing-recovery.lock` before the ledger's existing lease and releases them in reverse order. Only explicit CreateNew can create the pair. Existing and QueryOnly require both originals; a standalone ledger with no journal is not silently adopted. Missing/corrupt/mismatched originals and orphan SQLite companions are preserved. Initialization failure can leave an orphan ledger or temporary journal; reopening does not repair or adopt it.

QueryOnly uses SQLite READONLY and rejects submit/retry. Inspection never applies, settles, rewrites or discards input. Cooperative lock files and SQLite reader bookkeeping may still require companion-file access. The API is synchronous and belongs in a bounded helper, not a render/stop path. Linking it adds no native startup, worker, periodic owner or collection authority.

## Exact bounded private record

`<ledger>.billing-recovery` uses `CMBJ1` framing: three canonical decimal byte-length-prefixed sections, followed by a SHA-256 corruption checksum. Sections are:

1. Metadata (at most 16 KiB): state, optional result, exact private ledger path, original preparation UTC instant, prior monetary sequence and digest.
2. Complete canonical immutable binding (at most 1 MiB), including all tariff/cycle/budgets and, for v2, private source/retention attribution.
3. Complete canonical chunk (at most 128 KiB), or null for Empty. V2 keeps the entire original source query/reply and exact power chronology.

The total cap is the sum of those budgets plus 128 framing bytes. The binding and chunk reuse the ledger's canonical codecs; committed state uses the same canonical arithmetic serializer. Frame lengths count UTF-8 bytes, reject leading zeroes/truncation/trailing data, and never split a JSON string by character count. The complete binding is compared with the caller's validated binding. Chunk and metadata parse separately, so a record exceeding 1 MiB does not widen shared JSON/IPC limits. In particular, the existing 48/64-KiB power-query envelopes remain unchanged.

Atomic replacement writes an exclusive non-inherited same-directory temporary file, flushes it, then replaces the journal; POSIX also fsyncs the directory. POSIX creation requests mode 0600; Windows inherits the selected private directory's ACL. An I/O or replacement uncertainty poisons that handle and preserves the on-disk original for a new inspection. Temporary files from a killed writer are not treated as committed input or automatically deleted/adopted. The checksum/revision is not authentication against a writer who controls the whole directory.

## State and retry semantics

- **Empty:** exact binding seed and no committed monetary input or fabricated zero total.
- **Prepared:** full original durably recorded before `BillingLedger::apply`. Inspection accepts either the exact prior committed prefix or the exact original already committed with the expected chained monetary digest. Even the latter remains Prepared until explicit retry confirms it.
- **Confirmed:** exact original, monetary state and expected chain head are proven committed. Repeating that exact confirmed chunk is Replay without journal rewrite.
- **Rejected:** a new submission returned a deterministic refusal with the prior committed prefix intact. The rejected original remains visible; a later corrected submission may replace it.
- **Diverged:** journal and audited monetary head do not prove either permitted relationship. It blocks submit/retry without repair.

Submit cannot replace Prepared or Diverged input. Retry requires the complete reviewed original **and** the exact inspected journal revision; a changed receipt, refreshed query instant or stale revision refuses. Retry uses only the retained receipt and never opens the live energy source. Failed retry leaves Prepared bytes unchanged, even for a deterministic failure. Storage uncertainty and a page-cap failure that poisons the ledger handle also retain Prepared rather than manufacture a rejection. Journal settlement failure after COMMIT leaves a recoverable Prepared/committed pair.

Every inspection returns the committed prefix separately from the pending original. It checks original preparation time as well as the ledger's observation/query clocks. Retry time may advance but cannot replace the original query/observation/preparation instants or influence publication eligibility. Source ancestry and exact monetary arithmetic are still validated by the v2 ledger on admission/replay. No retry generates a new source receipt.

## Verification

`billing_pending` checks ordered leases, QueryOnly refusal and byte preservation, Empty/Confirmed/Rejected/Diverged, exact replay and stale revision rejection, source-offline inspection, clock/binding/capacity guards and orphan refusal. A maximum-composition escaped tariff binding plus 512-interval chunk makes a record larger than 1 MiB; it round-trips on a Unicode fixture path. A separately tested oversized binding refuses before creation. A 48-fractional-digit power/rate product that would need 49 digits correctly refuses; the valid large-record continuation uses 47 power digits without relaxing arithmetic. A 1-MiB SQLite page-cap fixture proves initial failure and subsequent failed retry retain the same Prepared original and prior committed head.

`billing_pending_crash` starts independent owned fixture processes at flushed-temp/pre-Prepared-replacement, durable Prepared, pre-COMMIT, post-COMMIT and post-settlement exits; it also injects settlement failure. After removing the owned synthetic energy source, it reconstructs and explicitly retries identical receipt bytes, proving one monetary event and unchanged source/pricing instants. Failed retries, rehashed malformed states/bindings/framing, absent files, divergent confirmation and artifact seam exclusion are checked independently. The maximum source suite also persists/reopens the combined 64-channel/512-interval/128-reading/32-window receipt, whose journal exceeds 64 KiB.

Executed evidence:

- Windows/MSVC focused CTest **10/10** passes: pending **94**, independent pending crash **198**, extended source **484**, source storage **32**, legacy billing/recovery **90/41**, accrual **187**, the **1,200-case / 9,825-prefix / 58,950-comparison** accrual oracle, and native-build/document checks.
- Linux/WSL/direct GCC pending **94**, independent pending crash **206**, extended source **484**, source storage **32**, legacy billing/recovery **90/41** and accrual **187** pass. Linux own-code/helper/probe **ASan+UBSan** passes that selection; the final pending/page-cap/malformed-database cases were rebuilt and rerun. Eight additional crash assertions are POSIX permissions and symlink checks. Unchanged energy dependencies/helper artifacts were reused from P-05j's instrumented build.
- The initial large-frame test hit the legitimate 49th fractional-digit refusal; it now verifies rejection and a representable continuation separately. Two inline Linux wrappers lost shell quoting before linking; script-based reruns completed. Neither failed wrapper is counted as a pass, and no decimal or warning policy was relaxed.

These are CPU/fixture/WSL and logical-capacity tests; physical disk/full-power-loss, installed helper trust/lifecycle, real sources and runtime qualification remain separate. No new named-timezone or UI/PTY qualification is claimed.

Next: bounded monetary helper transport and background ownership, including revision-bound full-original review/transfer larger than a single existing response; native source/configuration/cycle/archive handling follows. No public wire budget is enlarged or original discarded to fit a later transport.

[P-05l](billing-query.md) subsequently supplies parent-bound readonly inspection and complete-original paging under a single view digest. Write/retry transport, native review and background ownership remain separate.
