# Native economic command sessions (P-10j)

M-OPTIMIZER/M-SUPERVISOR; R-OPT-001/003 and R-SAFE-001/002; T-OPTIMIZER/T-INTENT; D-83.

`GameWorkerSession` can now connect [decision-bound commands](economic-command-owner.md) to its actual [native worker owners](worker-process.md). An empty economic policy vector preserves the existing game-only path. A nonempty vector must contain one complete frozen command/evaluation policy and work plan per logical slot. Ordinary CLI factories remain empty; linking this core does not activate optimization or qualify financial/work/safety sources.

## Parent and incarnation ownership

The session starts one `EconomicEvaluationOwner` and one `EconomicParentBudget` per configured slot under the exact borrowed parent intent/token. The command lease must match the fleet policy; plan, guard and evaluator policies are validated before any worker launch. Failed construction/start cancels already-started owners. No new explicit session is created.

Each worker incarnation gets its own `EconomicCommandOwner`, borrowing that slot's retained evaluator/budget and actual issuer. Initial Pause is submitted by the existing native process owner. Only after the real Paused receipt settles revision 1 does the command owner start and become the sole subsequent issue/acknowledgement caller. Initial admission does not bypass its guard or restore an old application state.

The slot retains all prior native owners until final joining. The declaration/destruction order keeps the borrowed evaluator and budget alive until every incarnation's command owner/guard is destroyed. A queued replacement exposes no predecessor command/application snapshot as its current state; the parent evaluator and budget remain visible separately.

## Current conditions and original tickets

Each tick first consumes the current aggregate safety evidence and fixed game runtime. For economic ordinary candidates, only game Off or NormalEligible is normal; unknown, active, paused and unqualified coexistence states cannot authorize these profiles. The evaluator receives these exact current conditions and the latest notified context revision before the command owner is polled. A source notification takes effect on the next tick; zero or rolled-back revision latches a fleet fault. Full plan changes require another explicit parent, not mutation of the current slot.

The private schedule exposes its original market/evaluation/retest tickets. `submitEconomic` binds input to the current context, complete plan, original ticket times, actual parent generation and exact current conditions through the real evaluator. Only the owned CPU calculation may complete an evaluation. External completion is limited to the other lanes or explicit rejection/cancellation of an unsubmitted evaluation; it cannot inject a completed study.

An initial ticket can predate the actual initial Pause. It cannot be restamped to become post-Pause evidence. Explicitly settle that unsubmitted ticket and wait for a new independently scheduled request. Expired tickets retain ownership until settlement and use the configured retry interval. The session does not fetch data, manufacture profile samples or perform an external retest merely because a ticket exists.

External Unreaped remains observable after stop. While active, a callback that reports terminal evaluator/ownership failure immediately revokes the whole fleet before returning, rather than waiting for the next tick. Private snapshots contain original studies/commands and must not be copied to public stats or logs.

## Native delivery, revocation and replacement

Each newly issued command is submitted once to the exact `WorkerProcessOwner`. Failed submission or a command-owner revocation request halts every slot, cancels publication and revokes all owned channels. No second Pause overwrites a pending Run. Accepted stays pending; only the original boundary WaitApplied installs an application fact. Settled loss of ordinary eligibility uses cooperative Pause, while loss of a pending command's full source/safety deadline revokes immediately. Cached-source and early-Pause headroom rules are unchanged.

The internal fleet snapshot now exposes `commandPending`, recording control-thread settlement separately from child output. Issuance sets it before native submission, so failed delivery cannot look settled. Revocation/process exit do not silently clear unresolved commands. It is not a new public telemetry field.

Replacement requires actual native Exited, actual reaping, exit code 1 and settled bookkeeping before disconnection/exit. The command owner must have started. Only then is its issuer stopped and the command owner explicitly retired, accounting the final original receipt through the parent budget. Calling command-owner stop before retirement would lock that budget and is deliberately avoided. The supervisor's existing retry delay/count policy still applies above this admission seam; the session itself does not automatically relaunch.

Exit 0/7/8, forced/failed ownership, unknown application, missing initial admission, unresolved command or unproven reaping cannot release a replacement. A receipt not consumed before terminal/disconnected observation is conservatively unknown, even if the child emitted it; this bridge does not infer its accounting from stdout or process exit. Actual exit 1 alone is not enough. New keys and sequential generations preserve the same parent-slot cooldown/transition history and evaluator. Replacement still needs a study strictly after its own initial Pause; a retained old study is not new authority.

Stop revokes channels and publication first. Final joining waits for native owners before the evaluator threads and fixed game runtime. No callback, late original, old token or new explicit epoch revives this instance. Genuine driver/kernel/disk or permanently blocked computation joins remain separate physical/runtime qualification work.

## Verification scope

`test_game_economic_session` launches only self-owned CPU peers using the actual `WorkerControlClient`, native pipes, random-identity helper and production session/evaluator/guard/issuer. Financial/work inputs use explicit synthetic qualification and fixture UTC mapped to the real monotonic clock. Game mode is Off; existing native game/detector suites cover that independent path. No GPU, game or external provider is started.

Checks cover initial no-data Pause; actual zero-wait application; same-wait fresh-study rebinding without extra transition; context/thermal cooperative Pause; explicit 10-ms application; six two-slot pending withdrawal/stop cases; original deadline shortening; reaped runtime-error replacement; retained evaluator, key/generation continuity, cooldown and exhausted budget; clean/fault/unsettled exits; missing terminal receipt; malformed policies; exact/expired tickets; external completion refusal; late Unreaped and stop nonrevival.

The first Windows run reproduced predecessor command state leaking into a queued replacement. That case now reads only the matching current generation. A second apparent scheduling failure was a fixture mismatch: its five-second wait used the default thirty-second retry. The fixture now explicitly requests a one-second retry; production retry semantics were not weakened. A further callback-boundary test reproduced Unreaped becoming known before channel revocation; active submission/completion now propagates terminal evaluator state synchronously. The execution ledger records final cross-platform results.

Broader parallel Linux runs exposed two assumptions in the existing CPU fleet fixture. Child stdout could precede parent-side command settlement, so requesting thermal Pause then correctly revoked a still-pending Run instead of taking the test's intended settled path. That case now waits for actual `commandPending == false`. Separately, independently sampled host wall/monotonic milliseconds could shorten a deliberately equal-length cached safety lease and correctly revoke it before the headroom assertion. The fixture uses coherent mapped safety/session clocks; explicit skew-withdrawal tests still alter UTC, and the game-entry case keeps a shorter command horizon than detection freshness. Production freshness/revocation rules are unchanged. Repeated pending tests verify the corrected setup.

Windows uses the CMake native/economic targets and focused CTest selection. Linux directly rebuilds the changed session and both native session tests with GCC C++17, `-O1 -Wall -Wextra -Werror -pthread`, linking existing unchanged supporting objects. The sanitizer variant adds ASan+UBSan, frame pointers and leak detection without suppressions. These are real CPU/native-channel checks, not Linux CMake, production CUDA, qualified-source or physical GPU/game acceptance.

Final evidence: Windows focused **27/27**, followed by **3/3** rebuilt native cases after recording pending state before submission. Windows/Linux ordinary and Linux ASan+UBSan/LSan pass native economic **221**, existing session **126** and pending withdrawal **67** checks. Both Linux variants also pass **five additional pending repetitions**, runtime **67**, protocol **818**, native IPC **132** and parent-death/**9** CLI refusals. Windows includes command/guard/evaluation, Fraction-oracle, shared-loop, supervisor, native-build and architecture regressions. Architecture/import guards and staged privacy audits pass.
