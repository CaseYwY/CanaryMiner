# Readonly staged-input restart reconstruction (P-05o)

M-TARIFF/M-ENERGY; R-TAR-002–005, R-ENG-001, R-SAFE-001/002, R-PRIV-002 and R-REL-001 remain partial; T-TARIFF/T-ENERGY/T-INTENT/T-PRIVACY; D-54. This makes the [bounded staged input](billing-input.md) inspectable after the original parent has exited, using only an already approved binding and explicit monetary path.

## Read only with a complete caller-approved binding

`canary_billing_input_query_worker` accepts `CMBISQ1`: parent, generation/ticket, exact private path, approved canonical-binding digest, query UTC instant, page offset and optional expected view digest. It takes the stage lease, validates the bounded regular stage file and its checksum/canonical representation, and checks the requested binding. It does not open the monetary ledger or energy source, create a stage or directory, append, commit, reconcile, retry, settle or delete. Cooperative lock-file access remains necessary.

The response describes the actual stored stage, including Receiving, Issued and Complete. **A certificate in this view is the previously stored stage outcome, not a fresh query of the monetary ledger.** The pair may have advanced or disappeared since that certificate. Its type and native presentation must retain this distinction. New submission still requires reviewed-current monetary state and the live ordered CAS checks.

An absent main stage is reported explicitly with no original/outcome or monetary totals. The helper separately probes the fixed `.tmp` slot and returns `replacementPending` whenever it exists, including when the main stage is absent. The temporary bytes are neither read nor adopted. An inaccessible path is unavailable, not proven absent; unexpected stage types, symlinks/reparse points, corrupt/oversized records refuse without rewrite.

## Reconstruct exact bytes without retransmitting a large binding

`CMBIV1` contains the canonical manifest, phase/certificate/revision, received byte count, received-prefix digest, temporary-slot flag and only the received **chunk** prefix. The parent already has the complete approved canonical binding. It recreates the exact binding prefix/full binding locally, appends the returned chunk prefix and verifies both the prefix digest and the complete CMBI1 stage revision. A rehashed but altered partial binding prefix cannot match this reconstruction.

- Receiving before the binding finishes has an empty chunk prefix and no typed original.
- A partial chunk remains an exact private byte prefix, possibly ending inside UTF-8/JSON. It does not become a valid rule, power trace or monetary input.
- Only the complete manifest lengths/hashes and canonical source/clock/sequence checks produce a typed original.
- Complete additionally verifies the stored journal/state/head certificate against the full approved binding and original. A proven unchanged result has no monetary outcome.

Requests remain at most 32 KiB. `CMBISA1` carries at most 48 KiB of raw view bytes within the unchanged 64-KiB response budget. The view cap is 128 KiB + 32 KiB + 16 KiB + 128 bytes; at most four flights share one digest and total deadline. A stage/temporary-status change between pages returns Conflict and discards all fragments. Generation/ticket/offset, canonical framing, complete-view digest and final reconstruction must all agree before publication.

One client holds its single-flight admission through final validation and shares one 50–60,000-ms deadline across flights. Cancellation, Busy, Timeout and Unreaped remain explicit. The fixed helper executable and existing Windows owned-job / Linux checked-parent PDEATHSIG boundary apply. No native renderer/stop-path filesystem access or automatic restart action is introduced.

## Verification

Tests exercise absent stages, partial binding/partial chunk/complete-ready/Issued/Complete originals, near-1-MiB caller bindings, multi-page complete receipts and retained stage outcomes after the monetary pair disappears. They independently corrupt and rehash a partial binding prefix, reject changed views and wrong binding/clocks/identities/helpers, check locks/Busy/cancellation/deadline and preserve originals. Restart probes know only the approved fixture binding and path, remove their energy source, and obtain exact staged input through the real reader. Corrupt/oversized/non-regular files, ambiguous temporary slots and owned-parent death are covered; ordinary artifacts exclude test seams.

Executed evidence:

- Windows/MSVC focused CTest **16/16** passes: staged query/lifecycle **64/55**, input/crash **131/183**, extended source **489**, and the existing monetary query/retry/pending/storage/build/document regressions.
- Linux/WSL/direct GCC staged query/lifecycle **65/59**, rebuilt input/crash **132/191** and extended source **489** pass. Own-code/helper/probe **ASan+UBSan** passes the same selection with previously instrumented unchanged dependencies. The additional staged-query check substitutes the parent PID; four lifecycle assertions exercise POSIX symlink/FIFO refusal.
- The temporary-slot presence flag was added after the first run. One in-progress sanitizer wrapper encountered revised assertions with its older probe and stopped with a missing-output-key error; the final probe, helpers and selected tests were rebuilt together and pass. That interrupted intermediate selection is not counted as a completed run.

Full-original native review and a background recovery/compute owner still need to combine this **staging** snapshot with a separate current monetary snapshot and generation/revision-bound actions. Explicit partial/orphan management, runtime source/configuration/cycle/archive binding and production qualification remain open. CPU/WSL/sanitizer evidence does not qualify physical storage faults, deployed trust/global reaping, real sensors/rates, named-timezone/UI rendering or actual income.
