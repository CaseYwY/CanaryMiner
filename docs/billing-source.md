# Source-bound monetary receipts (P-05j)

M-ENERGY/M-TARIFF; R-ENG-001, R-TAR-002–005 and R-REL-001 remain partial; T-ENERGY/T-TARIFF/T-PRIVACY; D-49. This connects the [isolated exact-power query](energy-trace-helper.md) to the [monetary-prefix ledger](billing-ledger.md), still with synthetic tariff/power admission only.

## Prove continuation, not just an increasing sequence

An `EnergyHistoryAnchor` names an earlier committed source sequence and chain digest. `verifyAnchor` checks it against the same audited source under its existing lease:

- sequence 0 must match the immutable source binding seed;
- the current head must match exactly;
- retained raw event heads must match the independently audited event chain;
- the compacted checkpoint and its immediate predecessor are proven by the already retained checkpoint digest/original/prior digest;
- an older nonzero head whose proof was expired returns **DetailExpired**, not success; an ahead/mismatched head returns Conflict.

`CMETQ2/CMETA2` adds an explicit anchor/proof result to the existing bounded query. No-anchor calls retain v1 framing. Limits remain 48-KiB requests, 64-KiB responses and 512 chronological intervals. The helper checks recovery and ancestry under the journal/source leases; failed or expired ancestry supplies no complete trace. A larger sequence after a fork or rollback is insufficient. Raw-detail expiry may therefore stop new billing imports even though aggregate energy remains available. No chain reconstruction, archive adoption or automatic reset is implied.

## Explicit new monetary schema

`BillingBinding::source` opts a **new** database into SQLite schema v2, with canonical `CMBL2` binding and `CMBC2` chunks. It binds the private source path, complete source/basis/channel/history-begin description and exact retention limits, in addition to the complete tariff/cycle/budgets. Source history must begin no later than the billed cycle. V1 stores remain compatible with their original standalone fixture chunks; neither format adopts or migrates the other.

An empty v2 ledger has a source seed anchor but no monetary total. `sourceQuery` plans exactly `[committedBillingThrough,requestedThrough)` from the current immutable state. `makeBillingSourceChunk` requires an anchored, successful, fully covered exact-power reply. It freezes the complete request and response, normalizing only ephemeral generation/ticket/parent IDs to 1; IDs are not source facts. The chunk's pricing-observation time is the source checkpoint's original observation time, never query completion or restart time.

On admission the ledger checks source binding, exact outer/receipt trace equality, the requested prior source anchor against its own last committed source anchor, monotonic query time, and original observation time. Its ordinary sequence/contiguous-prefix/rule/clock/capacity checks still apply. Multiple adjoining slices can use one committed source snapshot; a later snapshot needs positive proof of the prior source head. A query planned before a competing monetary append cannot bypass these checks. A seed-anchored fresh query cannot erase an already committed monetary source anchor.

The raw query receipt, exact trace, derived monetary state, new source checkpoint/query time and chained monetary head commit in **one SQLite transaction**. Restart replays every receipt and arithmetic step and checks the complete stored state/hash chain. Receipts remain private local evidence, not exported account data, sensor authentication or protection against an actor who rewrites all facts and hashes. Persisting a helper reply does not qualify a physical meter or actual utility plan.

## Replay, clocks, bounds and failure

- A retained exact chunk replays idempotently. Re-fetching at a different query time produces different original input, not an implicit replacement for an uncertain receipt.
- Already committed receipts can be audited/replayed without reopening the live source. Removing or losing the source does not erase previously committed money, but new imports require fresh source proof.
- Current observation time, query time and replay/open time have separate monotonic checks. The tariff evaluator still uses the immutable source observation instant to evaluate publication eligibility.
- Missing, partial, Prepared, Diverged, forked, expired-ancestry or unsupported-origin input supplies no new monetary prefix. Failed calculations retain the previous state. Declared or native power is not relabelled as synthetic.
- Existing 128-KiB chunk, 512-interval, 4,096-batch, 131,072-interval, 1-MiB binding and logical page budgets remain. Large receipt strings have their own original 48/64-KiB codec bounds inside the local chunk envelope; public JSON per-string limits are not relaxed. No partial receipt, rounded trace or skipped source interval is accepted to fit capacity.
- This is still a synchronous helper-side library. A source COMMIT and monetary COMMIT are independent: a monetary crash may leave a newer source and the old monetary prefix. Durable pending monetary originals, isolated monetary write/recovery ownership and explicit recovery review remain necessary before native activation.

## Verification

`billing_source` uses the real parent-bound trace helper with both raw and compacted source schemas. It checks restart continuation, multiple slices from one source head, replays, exact cost numerators, stale/changed/missing bindings, clock rollback, missing/Prepared sources, source forks with greater sequences, and old/retained/current anchor proofs. A combined maximum fixture imports and reopens 64 channels/512 intervals plus a retained 128-reading/32-window receipt with maximum-length identities and large counters.

`billing_source_storage` independently inspects SQLite v2, kills only its fixture before monetary COMMIT, proves the original monetary/source checkpoint remains atomic, and explicitly resumes/replays. It rehashes corrupted receipts (including a valid seed reset, changed source, missing receipt, trace mismatch and framing downgrade) and requires reopen rejection with original bytes preserved. Offline inspection succeeds after removing only its owned synthetic source.

Executed evidence:

- Windows/MSVC: billing/query selection **6/6** and rebuilt energy regression **18/18** pass (**22 distinct** cases; two overlap). New source/independent-storage suites pass **480/32**, existing billing/recovery **90/41**. Energy trace/process/lifecycle, raw/recovery/compaction, authorization/capture, collector/runtime and build/document checks pass in the regression selection.
- Linux/WSL/direct GCC: new source/storage **480/32**, existing billing/accrual/recovery **90/187/41**, trace process/lifecycle **640/10**, trace/storage **6,329/54**, history/recovery/compaction **746/359/3,674**, crash probes **73/149/111**, authorization/reconstruction **1,481/124** and capture **1,392** pass. Linux own-code/helper/probe **ASan+UBSan** passes that selection, with a final rebuilt 480-check source suite after the additional zero/partial/native-origin refusal cases.

Initial fixture failures were corrected at the fixture: source replacement must remove its own READONLY-created WAL/SHM companions, while production creation still refuses orphans; a maximal timestamp was moved one day inside the supported calendar range so its next civil midnight exists. Field widths/counter/power/envelope maxima remain exercised. Production arithmetic and preservation rules were not weakened. No new named-timezone, UI/PTY, physical meter or deployment qualification is claimed by this batch.

Remaining: durable pending/recovery/compute ownership, explicit settings/notification/cycle/archive handling, real source/rule/cadence eligibility and production deployment qualification. No actual-income source, automatic mining control or native billing display is activated.
