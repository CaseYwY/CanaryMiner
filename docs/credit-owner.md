# Isolated bounded credit ownership (P-07b)

[P-07e](credit-cursor.md) additionally supports explicit fixture cursors via `submitPage(generation, revision, page)`: one outstanding job, predecessor/sequence checked against committed progress, Busy published on admission and persistent recovery required. The legacy submission/volatile behavior below applies to non-cursor streams only. Neither ticket nor pending receipt substitutes for the schema-v2 transactional checkpoint.

Follow-up: [P-07d](credit-review.md) adds native immutable-snapshot review and `retryReviewed(generation, revision)` to bind the user's final admission atomically to the reviewed state. It does not qualify a real source or start a production credit collector.

**Offline/synthetic host integration only.** This batch adds a real native ledger helper, serialized bounded background jobs and immutable results; no production supervisor starts them. `FinancialView.enabled` remains false, no real source is qualified, and there is no new mining authorization, account/credential connector, manual income importer or today's forecast. [P-07a](credit-ledger.md) remains the durable-fact/schema contract; accepted-share history and its owner are unchanged.

This document records the initial P-07b volatile contract. The later [P-07c durable intent/receipt layer](credit-recovery.md) is now the owner's default; explicit `persistentRecovery=false` retains this compatibility behavior. Its smaller persistent IPC bound, restart inspection, read-only receipt verification and failed-retry rules have their own evidence and do not qualify a real source or production activation.

Requirements: R-INC-001/003–007, R-SAFE-001/002 and R-PRIV-002, all still partial for their full product scope. Modules: M-LEDGER/M-SUPERVISOR/M-PRIVACY; tests: T-LEDGER/T-INTENT/T-PRIVACY.

## Why a process, and why one transaction job per helper

Putting synchronous SQLite on a thread alone does not bound filesystem open/flush/close stalls. `CreditLedgerOwner` owns one background thread and one `CreditProcessClient`; the client runs an installed, explicitly supplied **absolute local helper**, never a source-supplied command. Each helper:

1. Decodes one bounded versioned request containing the file, immutable source contract, limits, frozen UTC instant, optional batch and 1–4 exact queries.
2. Opens a fresh single-owner `CreditLedger`, revalidating the existing file and binding.
3. Optionally applies one atomic batch, then queries under that helper's exclusive local-store lease.
4. Closes the store/lease before returning a bounded typed response and exiting.

This removes shared mutable SQLite metadata from the parent and isolates potentially blocking destruction too. Queries in the same job see the same facts because the store lease excludes other compliant writers until all queries finish. The schema is still v1; no accepted-history, UI/runtime preferences or database migration is introduced.

The file lease is **per helper operation**, not an idle lifetime collector lock. Another compliant writer may open the same bound ledger between jobs; each new job rereads/validates metadata and the source's revision-stream sequence. There is no cached parent database handle or promise of exclusive ownership of an idle file. This design trades repeated bounded integrity/audit work for a simpler kill/recovery boundary. Large-store throughput is not qualified; jobs can time out rather than skip checks or claim complete data.

## Bounded local IPC, not shell arguments or public JSON transport

`local_task_process.*` is a separate local native mechanism; it does not disguise private facts as public price requests or change the existing HTTPS helper. The core issues no network request and never supplies an arbitrary shell command, SQL statement, plugin or executable from a provider.

- Request <=256 KiB; response <=64 KiB; at most one child per client. A 50–60,000 ms monotonic deadline and cancellation are checked between nonblocking parent IPC polls (requested 5 ms). Each termination/reaping attempt has a 250 ms observation budget. An unreaped child reports `Unreaped`; that client will not replace it while it remains unreaped.
- Windows: anonymous page-file-backed mapping, with a **read-only inherited duplicate** for the child; no potentially blocking parent pipe writes. Only a mapping handle/length and a fixed protocol flag are command arguments. The response is a bounded drained pipe. Explicit inherited-handle allow-list, `NUL` input/error, minimal `SystemRoot` environment, no console, suspended spawn, kill-on-close job and active-process limit 1.
- Linux: local `AF_UNIX` socket pair for request input, nonblocking `send` with `MSG_NOSIGNAL`, EOF after the complete request, bounded nonblocking reply pipe, separate process group, empty signal mask and explicit signal defaults. `posix_spawn_file_actions_addclosefrom_np` requires glibc >=2.34; unsupported implementations fail closed rather than inherit unrelated descriptors. Parent `SIGCHLD` must be default and must not use `SA_NOCLDWAIT`; other parent policy is rejected. The application must not run another reaper against this client's PID. Already-reaped/ECHILD observations do not trigger a later numeric-PID/group kill.
- Both platforms drop the ordinary user environment, including proxy/key-log/account variables, and use an explicit executable path with no PATH lookup. Ledger paths, source IDs and amounts are not diagnostic/command-line text; the helper executable's own path remains ordinary OS-visible process metadata. UTF-8 file paths and spaces/non-ASCII are tested. Windows UNC/device roots, alternate data streams and embedded NULs are rejected lexically; callers must still supply a protected **local** installation and data parent. Drive letters/mounts are not proof of a local filesystem.
- Strict `CMCR1`/`CMCA1` JSON array schemas enforce exact shape, generations/tickets, source qualification, integer/time/enum bounds, canonical decimal strings, query coverage/completeness consistency and result limits. No unknown-version/extra-field tolerance or raw error echo. Source batch limits remain 512 distinct IDs/128 coverage intervals. Unsupported contracts are rejected before file creation. A malformed/oversized response is not an income snapshot.

This is **lifecycle isolation, not a security sandbox, encrypted IPC/storage, library authentication or a universal real-time guarantee**. Spawn/loader/kernel calls and genuine uninterruptible OS I/O cannot be made bounded merely by polling. Actual stuck-kernel termination/reaping, parent death, hostile installation/parent directories, disk faults and clean-machine loader/permission qualification remain release gates. On unresolved OS failure the application must retain the faulted client, not repeatedly construct replacement owners; global quarantine/reaper UX is still pending. The test fixtures stall ordinary helpers in userspace; they do not prove D-state filesystem behaviour or physical power loss.

## Queue, immutable snapshots and generation cancellation

`CreditLedgerOwner` defaults to 8 jobs and 1 MiB of encoded request reservations, including the current in-flight job. Configurable limits: 1–32 jobs, 1 KiB–8 MiB wire budget, operation deadline as above, and the existing ledger limits. One additional bounded uncertain job can be retained for reviewed recovery. Wire budgets are not total heap/SQLite/WAL memory budgets; typed objects, codec trees and <=4 copied totals add separately bounded work.

- `start(nonzeroIncreasingGeneration)` is explicit and queues only a preflight query. An unsupported contract, invalid limits or failed preflight cannot enable income. Start/stop lifecycle calls are serialized; snapshot, refresh, submission and stop requests use a short in-memory state lock. The injectable UTC clock is a pure nonblocking clock, never disk/provider work.
- Default queries are exact rolling 24h and cumulative since declared source history, with their end frozen at enqueue UTC. Callers may explicitly supply up to four other intervals, including a later calendar-today integration. No periodic provider fetch, implicit refresh or fill-forward is added. Backward admission clocks are rejected; complete clock mapping/freshness policy remains the future runtime integrator's responsibility.
- `submit`/`refresh` return generation-bound monotonically increasing tickets. `Queued` means **accepted in volatile memory, not durably saved**. A full queue returns `Full` with no consumed ticket or sequence; the producer must retain unacknowledged source batches and honor backpressure. It must not discard a page or advance a source cursor just because admission succeeded.
- A single worker serializes helper jobs. Read-only `shared_ptr<const CreditOwnerSnapshot>` objects retain their generation, ticket prefix, typed reply, source binding and health. Previously returned objects never change. Source-as-of/observation times remain source facts: replay or refresh cannot freshen coverage. Empty/unknown coverage stays unavailable, not zero.
- A rejected apply/query/storage/transport result latches a fault and discards queued **unacknowledged** jobs. No automatic retry, catch-up loop, source replacement, database deletion or mining restart. Successful/definite reply statuses remain visible; first failure stops later queued mutations. `lastSuccessfulTicket` describes whole successful jobs, not a substitute for inspecting an apply acknowledgement when a later query fails.
- `requestStop()` signals cancellation, revokes future result publication and discards queued work without waiting for disk/helper shutdown. An in-flight batch is conservatively retained as potentially committed. A stopped snapshot can still report an in-flight reservation until `stop()` joins and publishes final zero pending reservations. The worker never publishes a late result after revocation. `stop()`/destruction joins outside render/mining/safety loops; no detached SQLite thread is left behind.
- Stop/settings/query/recovery never starts a miner. An owner generation is a data-job identity, not the session's desired-mining token. Higher generations do not accept older-generation submissions. New financial runtime wiring must still respect the P-03 user-stop and fault arbiter.

## Lost acknowledgement is not rollback

Cancellation can arrive **after COMMIT**. Killing a helper cannot undo already durable credit facts. Therefore:

1. An apply whose started child produces no valid acknowledgement (cancel, timeout, crash, malformed/overflow response) reports `writeOutcomeUnknown`. An acknowledged `Storage` apply error is also conservative/unknown, not a promise that a flush failed before commit.
2. The owner latches, preserves the exact original batch, discards later volatile jobs, and never calls it a confirmed zero or rolled-back write. Reopening/querying valid facts is allowed after an explicit stop/higher-generation start, but new batches are blocked by `NeedsReconciliation`.
3. Only an explicit reviewed `retryUncertain()` action replays that retained original source ID/sequence/content, with a new request ticket/current query instant. It is **not** an automatic stop/fault retry. The original source/observation times remain unchanged.
4. `Ok` or exact `Replay` resolves this recovery. `StaleBatch` alone does not prove that the old uncertain content committed, so it cannot clear the latch; conflict/storage failure still needs explicit ledger/source reconciliation. There is no destructive repair shortcut or fabricated corrected coverage.

The original P-07b retained recovery batch is memory-only. [P-07c](credit-recovery.md) adds helper-side durable pending/receipt state and exact restart reconciliation by default; native recovery/import/export UX and qualified collection/source integration are still not implemented. Real ingestion activation requires those capabilities, not merely this queue.

## Build, tests and rollout

Host targets: `canary_credit_worker`, `test_credit_process`, `test_credit_owner`, `credit_worker_fixture`. The ordinary library/helper do not include the private stall/crash seams. The fixture executable is separately compiled with `CANARY_CREDIT_WORKER_TESTING`; it simulates pre-open, pre-apply and post-commit stalls, malformed acknowledgements and output overflow using invented records only. `credit_worker_boundary` checks ordinary artifacts for private test seams; they are not shipped as a usable feature switch.

```text
cmake --preset tests
cmake --build --preset tests --target canary_credit_worker credit_worker_fixture test_credit_process test_credit_owner test_credit_ledger credit_ledger_probe
ctest --preset tests -R "^credit_" --output-on-failure
```

The new helper is built for host tests but is **not installed/started by the production app**. Future activation must select the reviewed installed sibling, package/authenticate it and its OS/runtime dependencies, and add explicit settings/review/collection/recovery controls. No user SQLite file, existing installed miner, donation semantics, CUDA work or credentials are migrated by this batch. Rolling back removes unused host code; preserve existing credit/history data.

Executed evidence (2026-10-28):

- Windows/MSVC focused CTest **37/37**, including existing settings/history/intent/ledger/market/energy/helper regressions. The new process suite passes **62 checks** and the owner **62 checks**; the owner also passes **5 consecutive CTest repetitions**. The ordinary helper/library boundary and Python import-side-effect tests pass. Initial test compilation caught a mixed string initializer list; it was changed to an explicit string vector, not worked around with compiler flags.
- Linux/WSL/GCC direct host build (`-std=c++17 -O1 -Wall -Wextra -Werror -pthread`) passes **65 process checks**, **62 owner checks** and the existing **99 ledger checks**. The extra Linux process assertions cover the inherited descriptor and parent signal-policy boundary. Instrumented own core, helper and test-fixture/helper-parent tests also pass **ASan/UBSan**; the OS SQLite runtime is not instrumented. Native Windows additionally verifies that the request mapping cannot be remapped writable by the child.
- The Windows selection retains crash-before-commit/WAL recovery and the independent arithmetic oracles. The electricity oracle still passes **1500 cases / 4000 exact/display comparisons** after its import guard/local-context correction. Architecture contracts pass. These selections are not full Python discovery, Linux CMake, physical disk failure, kernel-stuck/reaping, native GPU/game, live source/terms or production release qualification.
- Source/staged-byte/artifact and available-history privacy gates use the private untracked deny-list. Only source/tests/docs are staged, not helper binaries, databases, private configuration, build logs or measurements. No third-party implementation/runtime dependency is downloaded by this batch.

Cases include exact large values/canonical wire limits, unknown/partial versus zero, immutable snapshots, source/scope binding, replay without freshening, concurrent-writer refusal, bad response/reply-generation rejection, explicit inheritance/environment filtering, Unicode helper/data paths, backpressure with no consumed tickets, stopped queue discard/no late publication, automatic-fault latching, lost-after-commit acknowledgement, and exact explicit recovery. Tests also guard CLI-like Python helper scripts against import-time work: the previous electricity oracle now has a main guard/local Decimal context, so unittest discovery cannot execute its probe or alter other tests' precision merely by importing it.

API reference lookup used Context7's first-party Win32 material for file mappings/inherited handles; native tests verify the child cannot remap its request writable. This is not independent dependency/OS/security certification. Qualified real-credit providers, manual import/export/recovery UI, supervisor/financial activation, today's forecast, OS-failure/packaging and production release gates remain open; P-07c's later persistent journal is linked separately above. No real pool/account was contacted and no miner was started.
