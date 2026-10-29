# Exact monetary-prefix continuation (P-05g)

M-TARIFF/M-ENERGY; R-TAR-002–005, R-ENG-001 and R-REL-001 remain partial; T-TARIFF/T-ENERGY/T-PRIVACY; D-46. This extends the [pure electricity core](electricity-core.md) with bounded prefix arithmetic and a separate durable synthetic monetary ledger. It supplies no source qualification, background owner, native billing activation or utility-certified bill.

## Prefix versus settlement

`accrueBill` accepts a complete contiguous `[opening.through,end)` trace with `end <= observedUtcMs`. It returns an immutable ending state, provisional change, provisional total and, only at the explicit cycle end, a settled total. Amounts stay exact numerators over **3,600,000,000**; only display rounds half-even. The accumulated subtotal is pre-multiplier; provisional/settled totals include the plan's explicit single multiplier.

- Progressive tiers apply each effective revision to new usage while retaining the shared quantity register. All-units recalculates the tier component for the **entire accumulated quantity**; its change can be negative and is not merely this chunk's kWh times a marginal price.
- The minimum-cycle subtotal is applied once at cycle end, never once per chunk. `settlementDependent` is true before that boundary when a floor or all-units component can change the value assigned to earlier consumption.
- A cycle charge applies once at cycle start. Civil-day charges apply once per newly entered local date across chunks/restarts. UTC/fixed clocks and explicitly configured offline named clocks retain the core's DST/version rules.
- Non-whole-meter evidence may continue its own energy-only prefix. Shared tiers, floors and cycle/day charges still reject; GPU-board amounts cannot stand in for household-meter bills or incremental mining costs.
- Gaps, overlaps, missing dynamic prices, clock/version conflicts and arithmetic capacity return no derived amounts. Original observation time governs known publication rules; restart does not refresh it. Chunk equivalence assumes the same eligible immutable rule knowledge, not retroactive knowledge of a later announcement.

`evaluateBill` and `counterfactualBill` keep their previous complete-settlement horizon restrictions. In particular, a short all-units/floor prefix is provisional accrual, not an eligible shortened counterfactual bill. No supplier rounding convention, new rule family, automatic cycle reset or proration is introduced.

## Independent store and immutable admission

`BillingLedger` is synchronous, single-owner helper-side code. `canary_billing` is built separately and is not called from the miner, renderer, settings or safety loop. The only admitted origin is **SyntheticFixture** for both tariff and power, with a `fixture.` cycle key. Real telemetry and offline declarations cannot be relabelled as qualified billing input by the runtime; no such adapter exists.

The immutable binding includes the complete tariff/rules/publication times, source/basis/scope/adapter, explicit cycle, and all retention/capacity budgets. Caller-supplied arbitrary opening quantities are absent: creation begins at the explicit cycle start with zero internal accumulators. An **empty ledger has no monetary total**. A numeric zero becomes available only after a complete admitted zero-valued prefix. The opening's `declared` field is the pure evaluator's structural precondition, not proof of a real meter.

Each canonical chunk carries sequence, unique ID, immutable observation time and exact power intervals. It must continue the last committed prefix and advance the sequence. Exact retained originals return Replay without changing time or totals; changed content/ID reuse conflicts. Reordered/missing chunks, different evidence and future/regressed observation times reject. No rounding, dropped interval or silent reset is used to fit capacity.

Budgets: at most 512 intervals and 128 KiB per chunk, 4,096 batches and 131,072 intervals lifetime, a default 64-MiB logical database cap configurable within 1–128 MiB. The binding is bounded to 1 MiB, 128 revisions and 4,096 aggregate rule rows; individual calendar/band limits stay 64. Cycles retain the core's 370-elapsed-day bound. Limits are immutable for a store. There is no monetary compaction or raw archive; these bounds do not promise a real sampling retention duration. SQLite WAL/SHM/lease overhead is separate from the logical database page cap.

## Atomic persistence, inspection and failure

SQLite schema v1 has independent application ID `0x434d424c`. Optional system SQLite uses the existing loader/lease, WAL, FULL synchronization, zero busy wait and bounded SQL/value/cache settings. One transaction inserts the original chunk, its fully derived state and the chained SHA-256 digest, then updates the singleton head/state. In-memory publication occurs only after COMMIT succeeds.

Every open replays all retained chunks from the bound zero origin, re-runs exact arithmetic and compares every stored intermediate state, digest and final head. Rehashing a fabricated monetary state does not bypass arithmetic validation. The hash chain detects local corruption/recovery divergence; it is not source authentication or protection against an actor rewriting every fact and hash.

- `Existing` requires the original regular database and compatible binding/schema; it never recreates missing history.
- Explicit `CreateNew` refuses an existing file and orphan `-wal`, `-shm` or `-journal` companions, preserving them.
- `Inspect` opens SQLite **READONLY** and rejects `apply`; it can prove exact committed originals. The shared lease and SQLite's WAL-reader bookkeeping may still require companion-file access. It is not a hostile-path/no-filesystem-write sandbox.
- Storage/page-cap failures revoke the current readable instance. A page-cap failure returns Capacity; independent reopen audits the previous committed prefix. An ambiguous COMMIT outcome cannot be called rejected or auto-retried. A future isolated owner must retain pending originals and reconcile them explicitly.

Missing/corrupt/newer-schema/wrong-binding/oversized histories remain preserved. No adoption, migration, reset, repair, rename or deletion API is supplied. Tests use owned synthetic scratch databases only.

## Same-prefix remaining-cycle scenarios

`projectRemainder` uses one nonempty committed prefix for both complete whole-meter scenarios through the stored cycle end. It rejects a settled ledger, regressed decision time and incomplete/mismatched traces. The two futures remain caller-supplied synthetic scenarios; neither is appended as observed energy. The resulting difference includes nonlinear repricing/floor effects on the common prefix and preserves the core's signed result.

This is not today's GPU-board net row or an asserted whole-house baseline. It adds no revenue, FX, sales inference or optimizer permission.

## Verification and remaining integration

Focused targets: `test_bill_accrual`, `bill_accrual_probe`, `test_billing_ledger`, `billing_ledger_probe`; CTest `bill_accrual`, `bill_accrual_oracle`, `billing_ledger`, `billing_recovery`, plus explicitly configured `bill_accrual_named`/`billing_ledger_named`. Recovery's pre-COMMIT exit exists only in the separately compiled test library; ordinary artifacts exclude it.

- Windows/MSVC: **20/20** focused billing and affected forecast/energy/tariff/settings/supervisor/build-contract cases pass; explicit offline named-clock selection **5/5** passes. Ordinary/named accrual checks are **187/226**, monetary-ledger checks **90/103**, independent recovery **41**.
- Linux/WSL/direct GCC: the same accrual **187/226**, ledger **90/103**, recovery **41**, existing electricity **149** and legacy electricity oracle **1,500 cases / 4,000 comparisons** pass. Own-code **ASan+UBSan** passes these ordinary/named billing checks; the external named date-tz object is uninstrumented.
- The independent Python Fraction oracle passes on Windows, Linux and Linux sanitizers: **1,200 cases / 9,825 prefixes / 58,950 exact/display comparisons** each. Rebuilt Linux tariff/session-energy/forecast **1,071/233/112**, supervisor/settings/display **257/152/77**, forecast owner/lifecycle **113/10**, forecast oracle **1,500 cases / 9,000 comparisons**, dashboard **18** and native forecast/tariff/session-energy PTY **27/16/24** pass.
- Coverage includes 23h/25h named-clock restart/settlement, concurrent writer refusal, strict evidence/binding, empty versus measured zero, exact replay, a 1-MiB SQLite page cap, separate-process pre-COMMIT death and independently rehashed malformed state. Synthetic page exhaustion is not a physical full-disk/power-loss test.

An initial oracle transport emitted exponent-form decimals outside the parser contract; its serializer was corrected to fixed decimals without changing production parsing. Exact expectations use Fraction, not rounded Decimal division. Final focused suites pass after orphan-sidecar refusal and logical-page checks. No installed miner, real utility/account data or physical GPU was exercised.

Remaining work: exact observed-energy/source-checkpoint adapter; immutable source-to-billing commit identity; durable pending originals and bounded isolated helper/owner; explicit consent/configuration/runtime/reporting binding; cycle transition/archive/selection changes; real rule/source/cadence eligibility; deployment/global helper lifecycle; physical-disk and production CUDA/SASS/verifier gates. Rollback removes this unused library capability without modifying any private store or installed miner.

[P-05h](energy-trace.md) supplies the first source-read foundation: coherent exact power/coverage/checkpoint snapshots through an existing-only READONLY energy store. It does not yet persist source receipts in this monetary ledger or admit real power as a billing source.
