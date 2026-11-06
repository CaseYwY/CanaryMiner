# Current game guard/runtime revocation and force-clock clearing (P-14j)

M-GAME/M-SUPERVISOR; R-GAME-006/007, R-SAFE-001/002; T-GAME/T-INTENT; D-166.

The fixed game episode guard and runtime now withdraw current presentation/override authority on explicit parent stop or native epoch invalidation, even before the next timed update. The intent's existing denial-only caller classification prevents a stale task from stopping, changing clocks, cancelling an override or consuming a healthy newer episode/runtime. No detector, game, worker or mining activation is added.

## Current versus historical

Guard reads perform only memory revocation, not a clock refresh or new eligibility calculation. Stop clears armed/active force flags and marks Stopped while retaining the original episode, ten-minute loss latch and immutable profit evidence as history. Foreign reads get a preallocated empty Stopped view; foreign update/force/cancel calls do not touch the healthy episode. Every force request also checks actual parent permission before changing override flags.

The cumulative UTC/monotonic skew fault path now clears both force flags just like the other clock-fault paths. A ClockFault cannot retain an active force-resume display or accept a new override. This does not extend detection/profit deadlines or erase the underlying episode loss latch.

Runtime current state, guard, force, detection and deadline reads observe actual parent revocation without joining or invoking a detector clock/source. Revocation requests the owned detector's existing memory cancellation before any later post-drain join; pending/native exit facts remain that detector's responsibility. A foreign tick refuses **before** changing the healthy runtime's cached tick/deadline or cancelling the detector. A preallocated denied detection frame is not reaping/absence proof.

The logical revocation fields and owned children are explicitly mutable for const current reads; no cast removes constness. The guard's revocation allocates no replacement view. The detector's existing cancellation may attempt a caught immutable projection allocation, so this slice does **not** claim a zero-allocation or hard allocator/native latency bound for runtime cancellation. No current read refreshes time, executes a request or joins a thread.

## Reproduction and verification

`test_game_current_views` first reproduces all ten original scenarios in ordinary and ASan/UBSan builds: native/intent cached-force and loss-latch views, foreign episode cancellation, Off-runtime current state/cancellation, and a cumulative-skew clock fault retaining the active force flag. The clock-specific case fails one of seven checks; the other failures remain distinct from that clock bug. All ten pass after the repair.

The fixture uses exact synthetic financial loss evidence, manual synthetic detections and a runtime configured Off. It starts no detector/helper, actual game, device, public source, account, network or mining process. Two additional guard-only C++ `new`-refusal cases require zero allocation attempts during current revocation. These are not arbitrary allocator interception, physical-memory or hard-latency qualification. Twelve `game_current_*` CTest cases cover the complete retained selection; unknown modes refuse before construction.

Final fresh cross-platform, native economic/fleet consumer, full game/runtime, repetition, privacy and cleanup evidence is recorded in the execution ledger only after completion. Enclosing fleet views/actions, real-source/safety/measurement/game-experience, hard native bounds, export/deployment/physical/release and the full roadmap remain open.

All twelve final cases pass ordinary/instrumented modes against fresh **38-unit** production archives, plus five further repetitions each, with all five game shipping/test hashes unchanged. Existing guard/profit **139** and evidence deadlines **670** pass. Windows default-off full CTest passes **397/397**, with **17** affected cases repeated five times. Twelve native UI/control and seventeen economic/measurement suites also freshly rebuild/pass both Linux modes, with fresh Language/CLI/PTY and prior control-lifetime checks. Full exact counts, privacy and cleanup are in the ledger; no real-source, game-experience or release qualification is inferred.
