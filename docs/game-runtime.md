# Session-owned game detection and guard (P-11c)

M-GAME; R-GAME-001/004/005/006, R-SAFE-001/002 and R-PRIV-002; T-GAME/T-INTENT; D-64. `GameRuntime` binds one [native detection owner](game-detection.md) to one [episode guard](game-session-guard.md) for a single explicitly started parent session. It supplies eligibility and lifecycle coordination, not a mining actuator.

## Frozen lifetime and startup

Construction copies the explicit target/adapter, detection bounds, game policy and profit binding, and borrows a `SessionIntent` plus its existing token. The intent must outlive the runtime. All runtime methods and intent changes belong to the same control thread; only the detector runs in the background. The caller must invoke `tick` with current clocks and safety before using eligibility. Neither a retained guard snapshot nor a detector snapshot is a transferable mining permit.

Start is single-use. It checks the existing token and validates the guard's policy/clock **before launching detection**. The same validation applies to Off: an invalid clock cannot leave an apparently healthy Off runtime. Off launches no detector, even when supplied detection settings are enabled. An active policy requires an exact game-key match, a valid frozen target and explicitly enabled detection; failed admission latches refusal.

The runtime owns the guard and detector by value, with no reset, replacement or retargeting API. Editing the caller's original inputs cannot affect this lifetime. A new session epoch cannot revive an older runtime. The parent integration must retain this runtime across settings review and child restarts; constructing a replacement under the same live token is not an approved way to reset an episode or dispose of an unresolved helper. Cross-parent durable revocation remains separate work.

## Observation handoff and terminal states

`tick` consumes the detector's immutable snapshot directly. Only Waiting/Ready ownership with the exact parent generation and game key is admissible. Original observation revision, time and process-instance identity pass unchanged to the guard; the runtime never renews them with tick/receipt time. Waiting/Unknown, missing helper or incomplete scan withholds game eligibility. An unchanged cached observation becomes stale normally. Fresh compatible Absent observations must still satisfy the guard's exit debounce before clearing an episode.

The guard remains the only owner of episode identity, coexistence origin, retained profit evidence and nonpositive-profit lock. Thermal pauses, missing evidence and detection identity changes use its existing rules. Native `process.` episodes cannot acquire fixture coexistence authority even when every synthetic source/profile field is supplied. Real experience and economic adapters remain unqualified.

Detector ClockFault, Faulted, Stopped, Disabled, unexpected Dormant, generation/key mismatch or Unreaped causes terminal runtime refusal and detector cancellation. Guard faults and clock faults likewise revoke detection. `requestStop` first revokes the guard and requests detector cancellation; `stop` joins afterward. Stop takes precedence over invalid clocks, safety faults and a later explicit epoch. Retained guard episode/evidence stays historical rather than being cleared by stop.

`detection()` reads **current detector ownership**, including cancellation and a late Unreaped result after revocation/join. It is intentionally independent of the last consumed guard observation: the background scan may have progressed since the last tick. Previously returned immutable snapshots remain historical and cannot hide current ownership. Fault cancellation may change the live detector state to Stopped; the runtime/guard retain terminal refusal.

## Verification and remaining work

The CPU fixture suite exercises startup/Off, frozen inputs, pause policy, fresh-only exit confirmation, stale observations, incomplete/missing helper, unsupported coexistence, invalid policy/target/bounds, detector/guard clock faults, in-flight cancellation, immutable historical snapshots and old-token non-revival. Existing guard tests retain exact ten-minute zero/loss, later-positive non-revival and evidence-matching coverage. Runtime integration does not weaken native identity rules to manufacture an eligible synthetic game episode.

Regression tests first exposed two draft defects: Off startup did not propagate ClockFault to runtime state, and the public detection accessor retained a pre-stop snapshot. Startup validation now precedes all branches/helper launch, and the accessor reads the live owner. The actual owned helper is cancelled and joined in the stop test; there is no injected genuine kernel-stall/Unreaped qualification.

Executed evidence: Windows/MSVC focused **7/7**, with runtime **67** and existing game/profit guard **113** checks. Linux/WSL/direct GCC passes runtime/guard **67/113**, plus detector/native-parser/lifecycle **90/24/35**; the same selection passes **ASan+UBSan**, rebuilding the runtime, game core, detector, helpers and tests with unchanged instrumented arithmetic/credit/transport dependencies. Windows detector/native/lifecycle regressions also pass. Complete Linux native detection/lifecycle checks use only an isolated user/PID namespace and have no skips in these runs. Architecture/import checks pass.

[P-11d](work-pause.md) supplies the shared-loop cooperative pause boundary and donation accounting; [P-11e](worker-control.md) supplies the fenced command/receipt core. Native settings/target review, supervisor lifetime admission, bounded owned production transport and physical pause validation remain the next integration work. Ordinary miner startup does not construct this runtime. These CPU fixtures do not run a game or qualify physical pause latency, gameplay, production economics, installed helper trust or global process quarantine.
