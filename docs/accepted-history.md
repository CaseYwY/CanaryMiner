# Local accepted-share history

This fork records **observed, acknowledged user-share counters**, not earnings, pool account history or wallet balances. The dashboard and plain summaries show rolling 24h, 7d, 30d and all-local totals, collection start, as-of time and coverage warnings. Donation, rejected, stale, pending and unknown outcomes are excluded. Share difficulty is not normalized; counts are not an income formula.

## Collection and controls

- An explicitly started supervised run collects locally in `state_dir/accepted-history.sqlite3`. Idle settings, status and self-tests do not create history. Each state directory is one local installation scope, potentially spanning different pools, workers and payout accounts. It is **not** a per-wallet or per-device ledger.
- `--no-history` disables history collection/display for that run without deleting existing history. Native persisted runtime collection controls are part of the later configuration batch; this option is not a presentation preference.
- The supervisor reads each child's raw user counter, never its already-carried session total. A local session, worker slot and spawn generation identify its high-water mark. Repeated values do not add shares; a new generation starts at zero. Older generations and decreasing counters produce a coverage warning rather than unsigned subtraction or an inferred reset.
- Missing/malformed accepted fields are not interpreted as zero. Only an exact non-negative integer in a valid single-device stats document is admitted. Local worker stats disclose no new external information.

## Time and coverage

Windows use `now - duration < observed_utc <= now`. UTC is stored to one-second observation resolution. Minute summaries accelerate full-minute ranges; second records preserve exact boundary selection at that declared resolution. Pool acknowledgement timestamps are unavailable: a counter increase is assigned to when the supervisor observes it, not when the pool earned or credited anything.

Polling can miss acknowledgements between the last snapshot and worker exit, even after a graceful drain. Sampling outages, unexpected exits, uncertain shutdown tails and intervals between collection sessions are recorded as uncertain. An inter-session interval may simply be time when the miner was off; it is not fabricated as either mining or zero accepted shares. Crash recovery conservatively starts uncertainty at the last persisted heartbeat (normally every 15 seconds). The main panel shows the latest uncertain interval; recent interval records remain in the database. All totals mean **known observations only**, not proof of complete pool history.

Clock rollback/out-of-order observation times and detected wall/monotonic divergence greater than five seconds latch a clock warning. Counters remain deduplicated; rolling totals display N/A rather than silently asserting trustworthy windows. A separate last-seen watermark detects rollback after idle retention maintenance. There is intentionally no automatic clearing of this warning or timestamp rewriting. Historical clock repair needs a separately reviewed migration; all-local observed totals remain available.

## Storage ADR: D-17, accepted-history slice

Use SQLite transactions with WAL and `synchronous=FULL`, behind a small owned C ABI adapter. Windows loads only the OS `winsqlite3.dll` from the system directory; Linux loads the distribution `libsqlite3.so.0`. The runtime must expose the required ABI and be at least 3.31.0; this is an API floor, **not** a security-update pin. No SQLite binary/header is downloaded, vendored or redistributed. Keep the OS/distribution runtime patched. Missing runtime/capability leaves history unavailable, without stopping mining or substituting a JSON store.

Why not rewrite preferences JSON or invent an append log? High-water counters, window records and lifetime totals must commit together. SQLite supplies transaction recovery, checked integer storage and indexed range queries without an application-specific journal/recovery format. A statically bundled SQLite build would improve version uniformity but adds source/license/package servicing that is not needed for this optional slice.

- One background owner performs database operations; a 4,096-observation queue and immutable snapshots separate persistence from the supervisor/UI. Queue overflow or I/O failure makes history unavailable; mining does not invent replacement totals. Shutdown drains workers before joining persistence.
- An exclusive installation lease rejects a second history writer. SQLite operations use no busy wait. Counters use checked signed 64-bit storage; overflow fails without changing the committed total. SQL values are bound, not concatenated from worker input.
- `application_id` and schema version identify the store. An existing empty, foreign, newer, truncated or integrity-failing file is **not** reinitialized. Recognized files receive a `quick_check` before normal journal/schema writes. No automatic salvage or deletion is attempted.
- Thirty days of second/minute records and closed gap/source/session details are retained; lifetime total/start and uncertainty flags survive compaction. Cleanup runs at open and approximately every ten minutes. Freed database pages are reused, not synchronously vacuumed on the UI thread.
- The main DB page-count ceiling corresponds to 512 MiB at its page size. Auto-checkpoint is 256 pages, with a 1 MiB post-checkpoint journal-size target. Since external readers can block checkpointing, snapshots also stop history when WAL exceeds a 16 MiB high-water mark. These are fail-closed capacity limits, not claims about exact total filesystem allocation; a bounded batch can overshoot the WAL mark before the next snapshot. No unbounded retention or silent lifetime reset is used.

Use a trusted local, user-owned state directory; WAL is unsuitable for a shared/network filesystem. Newly created POSIX files use owner-only mode, while Windows uses inherited directory ACLs. This is local plaintext data, **not encrypted storage**. No wallet, account, hardware identity, tariff or machine path is stored in the DB. File/no-follow checks do not make an attacker-writable parent directory safe.

SQLite references: [C API](https://www.sqlite.org/capi3ref.html), [WAL and checkpoint limits](https://www.sqlite.org/wal.html), [PRAGMA durability/integrity/capacity](https://www.sqlite.org/pragma.html), [public-domain statement](https://www.sqlite.org/copyright.html). Transaction durability still depends on the filesystem/device honouring synchronization; crash tests do not prove physical power-loss behavior.

## Recovery, backup and rollback

If history is unavailable, read the displayed generic reason, stop the affected run and preserve the database **with its `-wal`/`-shm` sidecars**. Never discard a WAL to make an error disappear. Back up only after every writer/reader is closed, or use a separately qualified SQLite backup tool; copying only the live main file is not a valid backup strategy. No in-miner automatic repair/export/delete operation is implemented.

To run without writing history, use `--no-history`. To roll back the feature, an older miner ignores these files; it does not downgrade or delete them. A deliberate new history scope requires a different state directory or a separately authorized move of the entire stopped store; do not pretend continuity after a reset. Future financial-credit storage needs its own exact-number/scope ADR and schema, not reuse of share counts.

Executed evidence and unrun release gates: [history validation](history-validation.md).
