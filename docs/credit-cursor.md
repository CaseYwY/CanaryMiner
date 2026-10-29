# Atomic posted-credit source checkpoints (P-07e)

This adds an **explicit synthetic page contract**, an atomic committed source checkpoint and one-page producer admission to the [credit ledger](credit-ledger.md), [isolated owner](credit-owner.md) and [durable recovery protocol](credit-recovery.md). It advances R-INC-003/007, R-SAFE-001/002 and R-PRIV-002 (T-LEDGER/T-INTENT/T-PRIVACY). These requirements and the whole P-07 product remain partial: real attributable providers, collection/fetch scheduling, production activation, recovery/import/export binding and historical references still need implementation/qualification.

## Page contract and committed progress

`CreditSourceContract::cursor` is optional. Its `version` and `initial` token are immutable parts of the file binding. Only `SyntheticFixture` and a `fixture.` cursor-contract version are accepted. This contract means **repeatable, exclusive, consecutive pages in one stable revision stream**, including stable credit/snapshot IDs, replacement revisions and source/observation instants. It does not retrofit these guarantees onto a real provider's offset-based or expiring pagination API.

- Opaque tokens are **1–512 printable non-space ASCII bytes**. They are compared byte-for-byte, never decoded as URLs/offsets, trimmed, numerically incremented or logged. Quotes/backslashes are encoded as JSON strings and bound SQL values. Different source token alphabets, expiry/reset/rollover or mutable pages require a new reviewed contract, not truncation or a silent reset.
- Each page carries `CreditCursorStep{before, after}`. The first `before` equals the bound initial token; later `before` equals the **last committed** `next`. `after` must differ from `before`, the initial token and every previously committed target. This stream is acyclic; repeated/no-progress cursors reject.
- The source sequence is still consecutive and the snapshot ID unique. Same latest canonical page is an exact replay; changing either cursor is a conflict. Local rediscovery never refreshes original commit observation time. A past sequence returns `StaleBatch`, which is **not success** for a cursor owner or proof that old uncertain contents were committed.
- A genuinely empty page may advance to a distinct source-issued token, with the same explicit source contract. Empty rows or a progressed cursor do **not** imply complete/zero income. Coverage assertions remain separate and replace the previous declaration; pages never implicitly union coverage, erase missing records or split an oversized completeness claim.
- The producer retains unacknowledged original inputs. No helper ticket, `durableSequence`, pending journal's `after`, or local fetch success is a committed source checkpoint.

`CreditLedger::cursor(nowUtcMs)` returns a read-only `CreditCursorCheckpoint`: health, committed sequence, snapshot ID, next token, original source-as-of and observation times. A new stream has sequence 0, the initial token and no snapshot/times. Non-cursor stores report unsupported; a clock/integrity failure supplies no usable token. Snapshot values describe that operation's committed state, not a live connection to the database.

## One SQLite transaction

Only **new explicitly cursored streams** use SQLite schema **v2**. Each `snapshots` row gains `cursor_before` and unique `cursor_after`; the canonical latest batch includes its step. Current records, revision audit, explicit coverage, source snapshot, metadata and the cursor all commit in the **same `BEGIN IMMEDIATE` / `COMMIT` transaction**. There is no separate committed-cursor file and no two-file atomicity claim.

The transaction rereads the persisted binding and chain, compares the expected predecessor, rejects reused targets and validates the complete batch. Conflict, invalid data, group over-reversal, capacity failure or process exit before COMMIT rolls back both facts and cursor. Stop after COMMIT cannot undo either. A read transaction validates the checkpoint; the helper's cooperative lease remains held through all totals and checkpoint reads.

On reopen/read/apply, the bounded audit checks consecutive snapshot sequences, predecessor links, printable tokens, acyclic/unique targets, IDs, nondecreasing source/observation instants (including **empty pages**) and consistency with latest canonical metadata. SQLite uniqueness is not the only guard: the independent audit also detects repeated targets after schema-constraint removal. Existing fact/audit/coverage validations still run. This is consistency detection under a protected local directory, not authentication against a party able to rewrite all history consistently.

No cursor/detail evidence is pruned to stay within the existing snapshot/database limits. Audits are bounded scans, not constant-time or production-throughput qualification. Capacity/timeout can make the owner unavailable; it cannot authorize skipping a page.

## Owner admission and recovery

For a cursored owner, `persistentRecovery` must be true. Use:

```text
snapshot = owner.snapshot()
require snapshot.state == Ready and snapshot.pendingJobs == 0
read the committed checkpoint from snapshot.reply.cursor
fetch a qualified page from that checkpoint (future adapter responsibility)
owner.submitPage(snapshot.generation, snapshot.revision, page)
```

`submitPage` compares generation/revision, Ready/idle state, sequence+1 and exact predecessor **under the same mutex** as admission. At most one job may be queued/in flight for a cursored owner, including inspections. Accepted admission publishes Busy immediately and invalidates the reviewed revision. Concurrent producers using the same revision cannot admit two pages; an intervening refresh also makes a producer's old revision stale. Only the helper's completed, validated result can return Ready. The legacy `submit` and `retryUncertain` entry points reject cursored owners; explicit recovery uses `retryReviewed`.

Journal ordering remains: retain **Prepared original input → apply atomic facts+cursor → persist Confirmed receipt**. Both cursor tokens are included in exact retry identity. Restarts only inspect; Prepared/Diverged input stays blocked until reviewed reconciliation. A missing/malformed acknowledgement retains uncertainty even if commit succeeded. Exact retry returns Ok/Replay and persists confirmation without doubling facts or advancing to another page. A failed/stale retry cannot erase earlier uncertainty. Previously published owner snapshots stay immutable.

- A surviving recovery entry cannot recreate a missing ledger.
- A confirmed entry must prove the current latest batch; a different current ledger is Diverged, not permission for automatic repair.
- **A missing sidecar with nonzero committed cursor progress is unavailable.** The ledger alone cannot prove that no later Prepared page was lost. Both files are preserved and a new cursor page is refused.
- With no sidecar and an empty/no ledger, the system cannot distinguish an intentional first stream from simultaneous loss of the entire bundle. External authenticated backup/source reconciliation is not implemented; this limitation must not be called loss detection.
- Sidecar and ledger remain separate files; no automatic migration, rollback, erasure, token edit or recovery-source substitution is provided. Preserve the bundle and original producer inputs.

The native Privacy recovery page distinguishes committed progress from pending original BEFORE/AFTER. Tokens/identities are hidden until explicit local reveal; long tokens remain complete in scrollable review. Queue and receipt sequence labels retain their different meanings. A revealed checkpoint is not a bypass for NeedsReconciliation/Stopped/Faulted state. The production settings page still has no bound collector and opens no ledger.

## Protocol, compatibility and privacy

Cursor requests/replies use **CMCR3 / CMCA3**. The request carries the cursor contract/step; the reply has a separate checkpoint in addition to totals and optional recovery. Checks enforce shapes, types, bounded token IDs, generation/ticket, success-to-exact-batch agreement and matching source/observation times between successful totals and the committed checkpoint. Cursor requests remain <=48 KiB; replies <=64 KiB. A large provider page is rejected, never silently split/truncated.

The sidecar uses **CMCJ2** with the canonical original CMCR3 request and SHA-256 corruption checksum. Mode 0 remains representable for that retained original and the low-level synchronous compatibility API. The public cursor owner always requires persistent recovery; mode 0 is not a way to turn it into a durable collector. Direct independent writers can cause unreconcilable divergence and do not acquire permanent idle ownership.

Non-cursor streams retain SQLite **v1**, CMCR1/CMCA1 volatile, CMCR2/CMCA2 persistent and CMCJ1 journals. No existing file is upgraded or rebound automatically. Opening a v1 store as cursored, or v2 as non-cursored, rejects while preserving it. Changing initial token/contract version also refuses the existing binding. Older binaries cannot interpret v2 or CMCJ2; rolling back code is not permission to delete them or bypass pending work.

Tokens, paths, bindings and credit records are private local data. No new log/export/config/provider-disclosure surface is added. The system-SQLite runtime, helper installation, OS leases, trust/loader assumptions and non-authenticating checksum retain their earlier scope. No dependency is downloaded or bundled.

## Validation and remaining qualification

The native cursor test covers first/continuous/empty pages, missing/invalid steps, wrong predecessor, cycles/reused targets, rejected/over-capacity rollback, canonical replay/time preservation, schema/binding compatibility, strict wire/digest/size limits, missing sidecar/ledger, concurrent owner admission, refresh invalidation, Busy/fault/stop boundaries and private complete-token review. Real helper fixtures interrupt Prepared, committed-but-unsettled and committed-but-unacknowledged operations, and return a malformed acknowledgement. A recreated owner inspects and explicitly retries only where needed.

An independent process exits before COMMIT; a fresh process verifies both the old amount and old cursor. Python/SQLite then damages chain/meta/time/schema fields in synthetic scratch files and checks preserved refusal. The native PTY runs the actual settings with an injected fixture owner, checks hidden cursor defaults, scrolls long original tokens, cancels review, admits one exact retry and sends controlling-terminal SIGINT. The old non-cursor PTY and credit regressions remain part of compatibility evidence.

Executed evidence (2026-10-29): Windows focused CTest **47/47** passes, followed by targeted cursor reruns after additional empty-first-page and canonical-journal cases. Both platforms pass **419 cursor checks** and five consecutive runs, plus **52 independent process-crash/integrity checks**. Linux/WSL/GCC passes **419 own-code ASan/UBSan checks** with instrumented helpers/parent, **23 cursor PTY checks** and the existing **21 non-cursor PTY checks**, plus rebuilt legacy ledger/review/process/owner/journal suites (99/200/65/62/133 checks). Python discovery passes **123 tests with seven explicit fixture skips**; documentation contracts and ordinary-artifact seam scans pass.

The initial Python probe kept SQLite connections alive after their transaction context ended. Explicit connection closing fixed Windows cleanup locks and prevented the test's own outstanding WAL checkpoint from changing its preservation hash. The new long-token PTY needed scrolling beyond the cursor tail to reach the original records; it now tests that navigation explicitly. Neither failure was treated as production durability evidence.

Tests use MSVC and direct Linux/WSL GCC (`-std=c++17 -O1 -Wall -Wextra -Werror -pthread`) builds, not Linux CMake qualification. Sanitizers add `-g -fsanitize=address,undefined -fno-omit-frame-pointer`; OS SQLite is not instrumented. Genuine kernel/D-state I/O, parent-death/unreaped-helper quarantine, full-disk/physical power-loss, source terms/repeatability/attribution, clean-machine packaging, live provider collection, pinned verifier/protocol/CUDA/SASS and production GPU/game qualification remain unrun for this slice. Windows full Python discovery/GNU-toolchain/encoding limitations remain separate.

```text
cmake --preset tests
cmake --build --preset tests --target test_credit_cursor credit_cursor_probe canary_credit_worker credit_worker_fixture credit_review_fixture test_pearl_cuda_cli_host
ctest --preset tests -R "^credit_" --output-on-failure
python3 -B tests/test_credit_cursor_crash.py <cursor-probe>
python3 -B tests/test_credit_review_terminal.py <review-fixture> <credit-helper> <host-cli-fixture> --cursor
```

The test-only crash symbol remains confined to `canary_credits_fixture`; ordinary library/helper artifact scans reject it and the stall markers. Production supervisor activation, observed energy/billing persistence and independent reset clocks, real forecast/FX inputs, profiles/optimizer/game/TLS/i18n and full release qualification remain separate roadmap work.
