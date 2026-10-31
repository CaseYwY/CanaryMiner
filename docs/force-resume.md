# One-shot operator force resume (P-11o)

M-GAME; R-GAME-005/007, R-SAFE-001/002 and R-PRIV-002; T-GAME/T-INTENT; D-90. This slice adds one explicit, memory-only operator override for a **known** game or economic pause. It does not add a detector, metric source, persistent preference or new mining permission, and it cannot overrule a stop, fault, cooling, clock failure or unknown detection. Game policy still defaults **Off**; ordinary non-Off CLI activation remains refused before device discovery.

## Admitted press and precedence

`arbitrateMining` computes the same pause sources as before. Unknown game detection, for example an opted-in mode whose detector is missing, still pauses. The override is admitted only when the current constraints expose the exact known pause the operator can see:

- a valid, detected game under `Pause`, or under `Coexist` when coexistence is not qualified;
- an enabled economic gate without valid positive eligible data.

The pause mask is empty under an explicit stop, fault latch, thermal/critical telemetry limit, or unknown/missing game detection. Pressing the key in those states does nothing and creates no hidden permission. `MiningForced` is returned only while a non-empty admitted mask is covered.

Safety precedence is unchanged: `Stopped`, `FaultLatched`, `Cooling` and unknown game pause all win over a force request. The override never changes a temperature threshold, economic margin, donation split, work-wait policy, budget or command lease.

## Single use and automatic release

`ForceResumeControl` stores only the admitted pause mask. It is not written to any preferences file, runtime default or saved game policy. The guard exposes `ForceResumeEligible` and `forceResumeActive`; a second press cancels. Automatic release occurs when:

- the game pause disappears (confirmed exit or a qualified coexistence state);
- the economic pause disappears (valid positive eligible data);
- the pause-source set changes, including a new pause added while another is overridden;
- cooling, fault, clock fault, explicit stop or unknown detection replaces the known pause.

After release, a later pause needs a fresh press. The fixed runtime and native fleet keep the same original detection, profit and safety expiry ceilings; the override does not extend them.

## Worker command interaction

A force resume issues only the Run the existing fleet would issue under an eligible state. Its lease remains clipped by fresh detection/safety evidence and the original command rules. When the operator cancels, a still-pending Run keeps its original lease semantics: the fleet requests Pause once that command settles, and an uncovered pending Run is revoked immediately by P-11n rather than overwritten. This means a cancel cannot manufacture a new permission or silently extend work beyond the command that was already issued.

The supervisor maps one `R` key press to the optional fleet seam. `WorkerFleetSnapshot.forceResumeActive/forceResumeAvailable` and the dashboard show **FORCE RESUME ON** and `[R] Cancel force resume`, or `[R] Force resume` when a known pause is present. Default fleets return no availability; no existing behavior changes. The override is per current process, dies with the parent and is not restored on restart.

## Verification and remaining work

Host tests cover admitted game/economic pauses, unknown detection refusal, cooling/fault precedence, auto-release on exit or recovery, source-set change, second-press cancel, stop/fault terminal release, runtime deadline support, native fleet Run/Pause behavior, dashboard hints and supervisor key handling. All inputs are explicit synthetic CPU fixtures; no GPU, game or production worker is involved.

Executed evidence: focused Windows/MSVC **6/6** test executables pass (`product_intent`, `game_guard`, `game_runtime`, `game_worker_session`, `product_display`, `product_supervisor`), the full Windows CTest selection passes **184/184**, and the same six suites pass on Linux under direct GCC in ordinary and **ASan+UBSan** builds (no CMake); the supervisor suite passes **251** checks from the repository root. The native fleet still requires a qualified safety/CLI binding, native source admission and physical game/GPU acceptance. The installed private miner patch and restart are separate operational work and are not part of this public slice.
