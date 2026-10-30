# Decision-bound command ownership (P-10i)

M-OPTIMIZER/M-SUPERVISOR; R-OPT-001/003 and R-SAFE-001/002; T-OPTIMIZER/T-INTENT; D-82.

`EconomicCommandOwner` composes the actual evaluator, [temporal guard](economic-choice-guard.md), [parent-slot budget](economic-parent-budget.md) and worker issuer. This default-off, single-control-thread object issues real bounded protocol commands from its own freshly checked decision, rather than accepting a caller-supplied point or copied guard snapshot. Ordinary startup does not construct it. Profile/reward/market qualifications remain synthetic-only.

## Ownership and immutable Run originals

Ordinary `start` requires that exact issuer's revision-1 Paused receipt. [P-09c](work-experiment-owner.md) adds `startAfterMeasurement`, a one-use release from the actual completed experiment owner, preserving its initial admission, complete originals, current exact Pause and original clock anchor. An arbitrary noninitial Pause is still refused. The measured path requires a new study strictly after its separate handoff barrier. Its evaluator, guard, issuer and budget share the identical explicit parent intent/token. The full plan and policies freeze for the incarnation. After startup, this owner is the sole issuer and acknowledgement caller; out-of-band issue or settlement faults instead of guessing the missing history.

Each Run retains an immutable `EconomicRunBinding`: exact private command/identity/revision/lease/wait, original selection clocks, complete guard decision and policy, exact immutable study (including profile, reward and scoring/source inputs), and the caller's complete safety/game deadline. The point and wait must agree with both the frozen plan and the permitted study candidate. A later record replaces only the current retained pointer; existing originals cannot be restamped or substituted. No original/private identity is added to public stats.

One unresolved issuer flight blocks every other command. Poll returns a new command once; it neither queues repeated deliveries nor performs native I/O. Accepted remains pending. Only the exact WaitApplied stores an application fact for the retained Run. Duplicate old receipts cannot settle a newer flight. The original command and first application timestamp remain distinct from selection, delivery and receipt consumption.

## Current context, source ceilings and revocation

The enclosing control thread updates the evaluator's current context/conditions before polling this owner. The supplied permission deadline is the current complete safety/game ceiling, not a delivery-margin-adjusted zero. The command horizon is 100–5000 ms. Run expiry intersects that horizon, the complete permission ceiling, the original economic source ceiling and any applicable confirmation ceiling. A short remainder requests Pause with the existing bounded headroom; headroom does not extend evidence.

The guard now exposes `sourceValidUntilMonoMs` separately from its current-worker-lease-clipped reference validity. Selection must still be valid **now** under the old lease, but a legitimate renewal may extend beyond that old lease while remaining inside original source validity. Otherwise every heartbeat would inherit the old expiry forever. No quote, profile, evaluation or confirmation timestamp is renewed by issuing a command.

A pending Run retains its own `EconomicStudyUse` and earliest ceiling. Every poll requires exact context, full-plan binding, safe conditions, complete permission covering the whole original command lease, and the exact published study/sequence. A safe in-flight evaluation replacement may retain the old study only through the evaluator's exact non-cancelled retention metadata. Completed replacement, cancellation, timeout, source withdrawal, shortened original ceiling or unsafe conditions revoke the issuer immediately and set `revokeRequired`; no free Pause slot is needed. Pending Pause can remain pending without economic Run permission.

A settled Run uses cooperative Pause when permission or economic evidence is missing. A replacement study that predates application cannot authorize continued operation through a PostApplicationStudy gap after the old source was withdrawn. Conversely, a valid newer same-wait decision issues a newly bound command immediately, even before the usual half-lease renewal point, so it cannot silently keep an old decision record. Same-wait application still does not spend another transition in the parent budget. With unchanged source proof, normal heartbeat suppression remains in place.

If the original Run was just applied and there is no strictly post-application study yet, the still-covered original command may remain within its own lease; Pause is requested before its expiry. Missing source proof cannot use this temporary no-new-decision path.

## Transport, retirement and historical facts

The enclosing native owner must submit each returned command or call `transportFailed`, and must immediately revoke/close its channel whenever `revokeRequired` is true. This memory-only object cannot prove delivery, pipe closure, complete-work drain, process death or reaping. It does not execute a process or supply a supervisor factory. [P-10j](native-economic-session.md) supplies opt-in native session delivery/revocation and actual reaped-incarnation admission; ordinary factories and qualified source admission remain separate.

Stop and transport/clock/protocol failures latch, revoke issuance and retain unresolved command bookkeeping. A valid late receipt can record its historical application after revocation, returning Terminal, but cannot revive commands, clear the parent stop or authorize retirement/replacement. Changing the explicit parent epoch cannot revive this instance.

Explicit retirement requires the exact issuer already Stopped, no unresolved flight and an exact latest settled receipt. The parent guard accounts that final original application before detaching. A replacement owner can then preserve the same slot budget, subject to all P-10h identity/time rules. This remains reference/command bookkeeping: actual reaping, eligible runtime-error exit and bounded retry admission must be independently proven by the enclosing process owner.

## Verification

Tests compose the real CPU evaluator, exact arithmetic, issuer and worker gate. They check actual issue/Accepted/WaitApplied phases, immutable originals, zero waits, excluded earlier candidates, source-clipped renewal, current-context changes, pending withdrawal, early headroom, stale original ceilings, cooperative Pause, historical late application, spoofed commands/receipts, out-of-band issue/settlement, final-receipt retirement, retained parent budgets, clock failure and stop nonrevival.

A separate blocking computation fixture tests retained old proof during safe replacement and terminal revocation on replacement completion, context change, unsafe conditions or timeout, including cancellation-ignoring late completion. Artifact checks exclude the hook from ordinary binaries/library. Two added regressions reproduced settled-old-decision continuation and delayed same-wait source rebinding (ordinary/controlled **467/535** checks, two failures each); both pass after preserving current Run proof across settlement and rebinding newer decisions.

Windows builds the new ordinary/controlled command targets plus P-10h's existing optimizer/protocol regression targets, then selects those 17 CTest cases including artifacts, both Fraction oracles, architecture and native build recipe. Linux directly compiles the changed command/guard/evaluator/protocol and both command/guard fixture variants with `-std=c++17 -Isrc -O1 -Wall -Wextra -Werror -pthread`, reusing unchanged supporting objects/executables. ASan+UBSan adds `-g -fsanitize=address,undefined -fno-omit-frame-pointer` with leak detection and no suppressions. These are CPU/in-memory protocol checks, not native process integration, Linux CMake, production CUDA, real-source or physical GPU/game qualification.

Final Windows focused CTest passes **17/17**. Windows/Linux ordinary and Linux ASan+UBSan/LSan each pass command/controlled **467/535**, guard/controlled **754/814**, artifact **7**, choice **45**, evaluator/controlled **149/215**, result use/study/schedule/profile **189/120/67/1230**, protocol **818**, and both Fraction oracles **384/1152** and **192/2496**. Architecture/import guards pass.
