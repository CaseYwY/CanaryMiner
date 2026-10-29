# Attributable posted-credit ledger core (P-07a)

Follow-up [P-07e](credit-cursor.md) adds an opt-in repeatable/acyclic fixture page contract, schema-v2 cursor steps committed atomically with facts, read-only checkpoints and revision-bound single-page owner admission. The v1 non-cursor contract documented here remains supported without migration. Real source collection/qualification remains separate.

**Implemented durable core, qualified only for explicitly synthetic fixtures.** This is not a working Kryptex/other-pool income integration. No production credit provider is registered, the supervisor opens no credit file, and `FinancialView.enabled` remains false. Accepted counters, wallet balances, payouts and market observations cannot create credits. Requirements R-INC-001/003–007 are partial; R-INC-002/P-08 calendar forecasting remains separate.

## Source contract before numbers

`CreditSourceContract` binds a file to one provider/contract version, asset, scope ID **and scope generation**, precision, posting-time basis, revision model, amount basis and declared history boundary. The initial supported asset is Pearl-mainnet-native, not a ticker-only match or a wrapped/BTC/account-converted substitute.

Defaults are unsupported. The only currently accepted qualification is `SyntheticFixture` with a `fixture.` namespace; enabling a real provider requires a separately reviewed implementation, not changing a runtime flag or reusing a fixture label. A typed input is not itself proof that a provider actually supplies these facts. The [reviewed Kryptex schema](research/market-and-credit-providers.md) still cannot establish the required records/attribution/time/finality, so actual income remains unavailable.

The initial fixture contract explicitly declares:

- Stable credit IDs; positive ordered revision integers with **replacement-value**, not replayed-delta, semantics.
- Posted UTC **milliseconds**, not discovery time, earning-time guesses or calendar-day buckets. Unknown units/time bases are unsupported.
- Exact fixed-point amount scale supplied by the contract (0–48 within the [decimal core](market-data.md)); no inferred Pearl atomic precision, binary-floating-point storage or silent rounding.
- Amounts represent posted net pool-credit quantities. Unknown gross/fee bases are unsupported; this does not establish a real provider's charges or permit double fee deduction.
- Worker attribution is exact and generation-bound. Account scope requires an explicit acknowledgement in the binding and stays account-scoped; one visible worker never proves exclusivity.
- A declared earliest included history boundary. Cumulative queries mean **since that declared boundary**, with coverage shown; never account/mining lifetime before it. A later history expansion requires an explicit migration/rebinding design, not silently rewriting a file's identity.

Bindings are immutable for this schema. Mismatched provider, contract, scope/generation, asset, precision or basis cannot merge into another file's totals. Identifiers are bounded ASCII tokens (1–128 characters, alphanumeric plus `._:-`); unsupported source identifier formats require an explicit adapter/schema decision, not truncation.

## Revisions and atomic batches

`PostedCredit` values are nonnegative. `PostedAdjustment` may be signed and references a known original `PostedCredit`, never a payout, balance or another adjustment. A replacement revision keeps kind/reference identity but may correct quantity or posted time. All current adjustments plus their original credit must remain nonnegative per credit group: an over-reversal cannot be hidden by another unrelated positive credit. A rolling window can legitimately have negative **net posted corrections** when its original credit lies outside the window; this is not negative physical mining production.

Each batch has an exact binding, a stable snapshot ID, a consecutive sequence, source-as-of time, local observation time, up to 512 distinct credit IDs and up to 128 completeness intervals. These are the synthetic contract's revision-stream semantics, not assumed properties of any actual pool. Pagination/source-snapshot consistency remains a real adapter's qualification responsibility.

- First ID/revision observation is stored with its source snapshot/sequence and that snapshot's source/observation times. Replays never rewrite first-observation provenance.
- A higher revision replaces the current value for its ID. A known lower revision cannot undo it; an unseen lower revision may enter the audit only, not the current total. Any conflicting content for the same ID/revision, including an old audited revision, rejects the whole batch.
- A same-sequence canonical replay is idempotent, including reordered input and redundant coverage normalization. Local rediscovery does not advance as-of/observation times. Same-sequence conflicts and reused snapshot IDs are rejected; old sequences are ignored and sequence gaps cannot advance coverage.
- A batch atomically commits current facts, revision audit, immutable source-snapshot identity/provenance, coverage and stream metadata. A conflicting/invalid/capacity-limited batch cannot consume a sequence or leave a partial correction.
- Late discovery preserves source posting time. Disappearing records/retention expiry do not delete facts or imply reversal; only explicit higher revisions/posted adjustments change them.

## Coverage, rolling 24h and valid zero

The exact query boundary is `fromExclusiveMs < postedAtMs <= throughInclusiveMs`. Rolling 24h uses 86,400,000 elapsed milliseconds; it is not a calendar today. Named-timezone today and its DST/remaining-day forecast are not supplied by this slice.

Coverage is an explicit source assertion, not inferred from returned rows. **Each batch replaces the previous completeness declaration**, rather than automatically unioning old claims forever. Overlapping/adjacent intervals within that declaration normalize; a one-millisecond gap stays incomplete. No interval may extend beyond source-as-of or into unknown prehistory.

Queries return separate known posted subtotal, requested/covered durations, matched-record count, scope, source/observation times and optional complete quantity:

- Complete coverage plus an empty posted interval is a valid zero.
- No rows and no coverage is unavailable, not zero actual income.
- Known rows or partial coverage yield explicitly partial data, never a complete total. Missing intervals are not filled or allocated uniformly.
- Asking before the declared history start preserves that unknown portion; it cannot become complete cumulative/rolling income.
- A source watermark behind the requested end leaves that tail incomplete. Neither a fresh HTTP receipt nor a replay manufactures current completeness.

Queries use one SQLite read transaction so metadata, current facts, audit and coverage come from a consistent snapshot. Application writes use `BEGIN IMMEDIATE` with zero busy timeout and no internal retry loop. Every amount is parsed/canonicalized as exact decimal text; SQLite floating-point `SUM`, binary conversion and display precision are never accounting storage.

The later [P-07c recovery layer](credit-recovery.md) adds `verifyLatest`: a read-only transaction compares normalized input with the **current latest** canonical batch. It cannot replay a missing batch and does not prove older content from an advanced sequence alone. No SQLite schema migration or financial activation is introduced.

## Latest-price references, not proceeds

`creditProductionReference` is a pure projection from a queried token subtotal and validated `MarketLatest`:

```text
known_reference_usdt = known_posted_tokens * latest_prl_usdt
known_reference_usd  = known_posted_tokens * latest_prl_usd
latest_prl_usd      = latest_prl_usdt * latest_usdt_usd
```

Changing market prices changes only these optional reference outputs, not a credit row, revision, scope, coverage or token total. Missing/stale FX leaves USD unavailable while valid USDT may remain available. Partial token coverage remains partial even with a valid quote. Arithmetic limits yield unavailable reference values, never a rounded/overflowed fact.

No sale state, current holdings or realized-dollar/profit field exists. Optional historical per-credit reference observations are not implemented here and must never replace this latest-price main-panel decision. The production financial renderer is not wired to this core.

## Storage, privacy and recovery

- A separate optional system-SQLite file, application ID `0x434d434c`, schema v1. It is **not** accepted history, UI preferences, runtime preferences or a credential store.
- Tables: immutable binding/stream metadata, current records, all observed revision facts with first source sequence, source snapshots with as-of/observation times, and normalized completeness intervals. Provider/scope/record IDs stay local and are never interpolated into SQL or printed as errors.
- Same maintained runtime ABI floor as history (SQLite >=3.31); added stable text/byte/limit bindings, with no SDK, downloaded DLL, extension loader or bundled SQLite code. This floor is not a security patch approval. Keep the OS runtime serviced.
- WAL + `synchronous=FULL`, explicit transactions, exclusive regular-file lease, final-file no-follow checks, private POSIX file permissions, bounded SQLite SQL/value limits, no attached databases and no automatic destructive repair. Unrecognized/newer/corrupt/binding-mismatched files are preserved, not cleared/recreated as zero.
- The local lease is now shared as a storage-only utility with accepted history; accepted-history schema/semantics are unchanged. This does not join accepted counts to token facts.
- Initial limits: 100,000 current IDs, 400,000 audited revisions, 400,000 source snapshots and a 128 MiB main-database page budget (configurable within explicit smaller bounds; maximum 256 MiB). Limits reject new work rather than pruning deduplication evidence or faking cumulative history. WAL/library/cache/process allocation are separate from the main-file budget. Audit/query scans are bounded by record limits, not a constant-time promise.
- Open/query validate types, canonical amounts, identities, group balances, revision/current consistency, coverage/snapshot consistency and source provenance. Unexpected tables/views/triggers are rejected. Integrity checks are not cryptographic authentication against an actor who can rewrite the whole private database consistently.

The caller must supply an absolute path in a protected **local** application directory. A trusted-parent/loader/OS-permission boundary is still required; no network filesystem, parent-directory race protection or encryption-at-rest claim is made. Store calls are synchronous, single-owner and not thread-safe merely because SQLite uses FULLMUTEX; do not call them from the render/mining/safety loop. [P-07b](credit-owner.md) provides a bounded background owner and one-operation native helper isolation; [P-07c](credit-recovery.md) adds default durable pending/receipt and exact restart reconciliation. No production caller activates them. Genuine stuck-kernel/parent-death/reaping qualification, source cursor/collection/recovery/export/delete UI and supervisor activation remain open.

There is no installed-user-data migration in this batch because the production app does not open this store. Rollback removes the unused core; do not delete accepted history or credentials. A damaged credit file needs explicit recovery/export/reconciliation design, not a menu's automatic reset or a new empty file at the same path.

## Validation and unrun gates

All records, source contracts, values and IDs in tests are invented. The private crash probe is compiled against a **separate test-only library**; its abrupt-exit seam is absent from the ordinary ledger target. It cannot import a real account response or start mining.

Executed cases cover unsupported contract/no file creation, exact identities/generation/scale/net/time basis, account acknowledgement, revision replacement/replay/conflict, signed adjustments/group conservation, posting-time corrections and late discovery, exact boundary milliseconds, explicit complete zero versus unknown/partial coverage, old snapshot/sequence handling, restart, concurrent writer refusal, source-provenance linkage, capacity rollback, exact values beyond binary-double precision, latest-price revaluation and absent FX. Process tests exercise crash-before-commit/WAL recovery, multi-record rollback, zero-busy-timeout contention, newer/unknown/corrupt/mismatched database preservation and unexpected SQL schema objects.

Windows/MSVC and Linux/WSL/GCC each pass **99 ledger checks and 40 process/provenance/integrity checks**. Windows focused CTest regression is **30/30**, including accepted-history and prior market/settings/supervisor cases. Linux ledger ASan/UBSan and rebuilt accepted-history **290 checks** pass with no sanitizer finding. Architecture contracts also pass. Linux compilation uses direct GCC commands, not Linux CMake qualification.

These are CPU/SQLite/process tests, **not** physical power-loss/full-disk/host-kill filesystem qualification, hostile-directory security, source access/terms/attribution verification, production GPU/game tests or live actual income. See [execution handoff](architecture/execution-plan.md) for the remaining whole-project objective.

SQLite API/transaction behaviour was checked against its [binding lifetime](https://www.sqlite.org/c3ref/bind_blob.html), [column-result conversion](https://www.sqlite.org/c3ref/column_blob.html), [transaction](https://www.sqlite.org/lang_transaction.html) and [WAL](https://www.sqlite.org/wal.html) references. Context7 supplied current first-party documentation. This does not qualify any pool-credit API.
