# Cooperative work-boundary pause (P-11d)

M-GAME/M-SUPERVISOR; R-GAME-004/005/006 and R-SAFE-001/002; T-GAME/T-INTENT; D-65. The shared `runMiner` loop accepts an optional borrowed `RunPauseControl`. Ordinary entry points leave it null; native game settings and the parent/worker command channel do not activate it yet.

## Request, quiescence and resume

The control exposes a bounded memory-only pause request and worker-thread boundary notifications. The independent stop flag always terminates the pause. A request is not proof of GPU quiescence, a session-start permit, a detector observation or an acknowledged generation/revision command. The future parent/worker channel must bind those separately, retain the same session guard, and revoke expired or stopped authority.

An initial pause is handled after admission but **before algorithm configuration, telemetry, source construction or preparation**. During a run, the loop handles pause after the preceding complete batch's proof/accounting path and any configured host wait. It drains algorithm lookahead before freezing productive accounting and publishing a paused boundary. Speculative next-attempt hints are disabled whenever this optional control is installed, even while running.

While paused, no new job fetch, preparation or search is started. The host requests at most 10-ms sleep slices and checks stop between them. Resume unfreezes the accounting hook, then the loop fetches a fresh job before further work. Additional checks after fetch and after preparation catch requests that arrive during those operations; they return to the boundary rather than launching the fetched job. A request racing the final check may wait for one already-started complete batch. Running kernels, preparation, source calls and OS scheduling are not asynchronously preempted, and no physical latency ceiling is asserted.

The helper rejects a reversed pause clock or failed lookahead drain. It never announces a successful paused boundary after a failed drain. Stop and failed pauses leave accounting frozen through run termination. If stop arrives inside the resume accounting hook or notification, accounting is refrozen before returning; an already delivered resume notification stays historical. After run exit the parent must use actual worker ownership/exit state.

The current scope refuses benchmark/formal, quota/duration/production evidence, owned work-range, private/public research execution, trusted retirement sessions, research/pipeline environment selectors and hardware-tuning combinations. Asynchronous submission is among the refused selectors, so this slice never treats an unsettled async proof as a completed pause boundary. The original null-control path retains its existing scheduling and hint behavior.

## Donation accounting and terminal faults

Both user and donation hooks implement the same pause transition. On entry the scheduler accounts the preceding interval and freezes its productive state; on resume it advances across the idle interval without credit, then restores the pre-pause productive state. The donation window remains owned, with the same remaining work obligation. Pause neither waives donation nor counts idle time as user/donation production.

The donation no-work watchdog preserves its **pre-pause age**. Only actual paused duration is excluded: a five-minute game pause does not become a pool failure, and a pool that was already failing to provide work does not gain a fresh timeout budget. Original real elapsed window/switch logs remain real elapsed durations.

Internal `kRunPauseFault` (11) is terminal to the product outer loop: it maps to the existing configured fault exit (8 in the miner), consumes no D3 restart budget and cannot restart a lane. A donation-lane control fault is reported as interrupted, not a fee-pool failure or completed donation window; outstanding obligation remains in the ordinary state. User stop still takes precedence. Wire protocol, fee destination, rate, random window policy and persisted fee schema are unchanged.

## Validation boundary

The deterministic pause helper checks no-op, stop, drain failure, clock reversal and bounded polling. A compile-isolated CPU fixture runs the **actual shared loop** with an in-memory job source and CPU algorithm: completed-proof-before-pause order, drain/accounting/notification order, initial pause, fetch/prepare races, fresh resume, host-wait composition, stop/failure and the original null-control path. This fixture omits device discovery and the Pearl protocol/session binding; it does not qualify GPU work or real protocol evidence.

The ordinary source is also compiled without the fixture macro. A positive/negative artifact check proves the test source seam is absent there. Linux's ordinary run-loop admission test uses aborting socket/connect/device-loader wrappers, covers initial pause before setup and inherited-selector refusal, and retains its existing admission regressions. Existing Pearl session host tests and host CLI/throttle checks are separate regressions.

Fee tests cover pauses before the first productive donation attempt, mid-window, user-lane accounting, unchanged remaining no-work timeout, stop while paused and terminal faults in both lanes. Windows/MSVC focused **11/11** passes, including pause helper/shared-loop **23/38**, fee-session/scheduler **127/6122**, intent **785**, throttle **48**, Pearl session, CLI and artifact regressions. Linux/WSL/direct GCC passes pause/shared-loop **23/38**, fee-session/scheduler **127/6122**, throttle **48**, ordinary admission/artifact checks and the rebuilt Pearl session host test. Linux **ASan+UBSan** passes the pause/shared-loop/fee/throttle/ordinary-admission selection with newly instrumented source and tests. Final review added four checks, first reproducing two resume-time stop failures; both now refreeze accounting. Architecture/import guards pass.

A first test build omitted the existing trusted-retirement compile definition required by `run.cpp`; the test recipe was corrected before runtime validation. Existing MSVC pool-size and optimization-override warnings remain outside this change. No production CUDA/SASS, physical GPU, actual pool or game session was exercised.

Next: a bounded generation/revision/expiry-bound worker control channel and supervisor admission, native mode/target review, private status reporting and permitted physical pause/game validation. No ordinary CLI/environment option starts this control, no installed miner is replaced, and native game coexisting economics remain unavailable until separately qualified.
