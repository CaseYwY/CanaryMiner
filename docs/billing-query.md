# Parent-bound paged monetary inspection (P-05l)

M-TARIFF/M-ENERGY; R-TAR-002–005, R-ENG-001, R-SAFE-001/002, R-PRIV-002 and R-REL-001 remain partial; T-TARIFF/T-ENERGY/T-INTENT/T-PRIVACY; D-51. This isolates readonly [monetary recovery](billing-pending.md), including complete source receipts larger than one IPC response.

## Explicit binding, readonly authority

The caller supplies its complete approved `BillingBinding`/limits in memory. The client sends only the hash of its canonical binding, the explicit private ledger path, generation/ticket, fixed query instant and page selection. A parent-bound one-shot helper acquires the journal lease, verifies that exact binding digest, strictly decodes the stored canonical binding and opens the matching ledger READONLY under the next lease. It cannot create, submit, settle or retry. `openQuery` is also a QueryOnly handle: its mutation methods reject.

The new binding decoder covers the existing canonical v1/v2 tariff/source representation, including exact optional amounts, calendar/date/dynamic rules, shared tiers, fixed offsets and explicit source retention. Canonical round-trip and existing binding validation remain mandatory; this adds no source/rule/timezone eligibility or private-timezone-data deployment. A near-1-MiB approved binding is read inside the helper rather than forced into an IPC request.

## Paged exact-original contract

- `CMBQQ1` is a canonical request bounded to 32 KiB. No request carries a command, source fetch, write input, arbitrary discovery root or relaxed open mode.
- `CMBQA1` has a small canonical header plus at most **48 KiB of raw bytes**, inside the existing **64-KiB response cap**. Lengths and offsets count bytes, so a page may split a UTF-8 sequence or JSON token; parsing happens only after complete assembly.
- The complete `CMBV1` view is bounded to **128 KiB + 4 KiB** and contains recovery metadata/revision, the exact committed monetary state/head and the full original chunk, including its source request/reply. The caller already has the exact binding. It reconstructs the complete CMBJ1 journal checksum from that binding and the returned original, rather than accepting an abbreviated receipt.
- At most **three** flights assemble a view. After page zero, every request requires the same complete-view hash, offset and total. Each helper independently re-audits under ordered leases. A changed journal **or committed state** returns Conflict; all accumulated fragments are discarded. There is no automatic restart, mixed snapshot, partial publication or truncated original.
- Final decoding verifies framing, complete-view digest, canonical state/chunk, source/monetary bounds and clocks, full journal revision and the permitted prior/after relationship. Prepared remains Prepared even when committed; Diverged remains distinct. Unknown/failed responses have no monetary or original payload.

Snapshots do not authenticate facts against a directory owner or substituted trusted executable; hashes detect corruption and bind the reviewed values. The helper performs the complete ledger/source-receipt arithmetic audit. The client does not claim to recompute every earlier monetary event from only the latest original.

## Lifecycle and scope

One client admits one complete multi-flight transaction. It shares one 50–60,000-ms caller budget across flights, checks cancellation and bounds both response assembly and operation count. The existing native transport supplies bounded termination/reaping and Unreaped outcomes. Windows uses the launcher's kill-on-close job; Linux verifies the parent and sets PDEATHSIG. Request data travels in local IPC, not arguments. The executable path is explicit; there is no PATH search or production install decision in this batch.

This is a synchronous background-only client, with no renderer/stop-path filesystem operations, periodic runtime owner or native activation. It reads already stored monetary originals without opening the live energy source. Isolated monetary creation/submission/retry, large-input staging, native review/background ownership and source/cycle/archive integration remain follow-ups.

## Verification

`billing_query` checks exact multi-page originals, changed-view rejection, generation/ticket/offset/hash/framing validation, Empty/Confirmed/Rejected/Diverged, missing/wrong binding, wrong workers, cancellation, whole-client Busy, deadline and malformed replies. Canonical binding cases cover calendar/date, signed dynamic prices, fixed offset, progressive/all-units and optional amounts. The maximum source suite additionally reads the retained 64-channel/512-interval/128-reading/32-window receipt through the actual monetary helper. The large pending suite verifies hash-selected readonly decoding of its near-1-MiB binding.

`billing_query_lifecycle` kills only its own synthetic parent, verifies the owned stalled helper terminates, and proves unchanged originals plus a fresh read. Separate Prepared and committed-but-unconfirmed fixtures remain inspectable after their synthetic source is removed. Ordinary helper/library artifacts exclude fixture and crash seams.

Executed evidence:

- Windows/MSVC focused CTest **10/10** passes: query/lifecycle **74/20**, extended pending/crash **96/198**, extended source/storage **485/32**, legacy billing/recovery **90/41**, and native-build/document checks.
- Linux/WSL/direct GCC: query/lifecycle **76/20**, pending/crash **96/206**, source/storage **485/32** and legacy billing/recovery **90/41** pass. The two extra query assertions check a substituted parent PID; eight extra pending assertions check POSIX permissions/symlinks. Linux own-code/helper/probe **ASan+UBSan** passes the same selection, reusing unchanged instrumented energy dependencies from P-05j.
- GCC initially rejected four misleading-indentation sites in the new binding/state codec. Statements were split, warning-as-error retained, then the complete selection rebuilt. The client also checks the aggregate deadline after final parsing before publication.

These tests do not qualify physical disk/kernel stalls, global helper quarantine, deployed trust/dependencies, real sensors/rates, named-timezone deployment or actual income. No new UI/PTY, real-source or native runtime activation is claimed.
