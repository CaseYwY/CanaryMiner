# Economic current-view and CPU lifetime revocation (P-14h)

M-OPTIMIZER; R-SAFE-001/002, R-OPT-001/002/003; T-INTENT/T-OPTIMIZER; D-164.

The [owned CPU evaluator](economic-evaluation-owner.md) already checks its explicit parent at operations. Its current result and schedule reads must also withdraw authority after explicit stop or a native ownership fault, without requiring another timed poll. A copied immutable study remains historical evidence, not current permission.

## Boundaries

An enabled, valid start binds one original `NativeProcessLifetime` before starting the schedule or CPU thread. The owned thread explicitly carries its original scope. It checks that lifetime and atomic cancellation before calculation and before returning a result. It never reads the control-thread-only borrowed `SessionIntent`. A successful cancellation-ignoring old calculation cannot publish or borrow a newer settled native epoch. A stopped/rejected instance cannot restart under another epoch.

`snapshot` and `schedule` perform only memory revocation checks. They do not sample time, invoke callbacks, allocate a replacement projection, join, fetch, or grant new eligibility. Explicit stop or invalidated native lifetime clears current study/point and previous-result replacement metadata, stops scheduling and requests CPU cancellation. Existing clock/condition/context rules still apply on timed operations; reading alone does not refresh them.

A foreign stale caller gets a preallocated Stopped/empty view. Its poll, submit and external completion cannot read, consume, cancel or settle a healthy newer owner's work. Outside that scope the healthy owner retains the same result and exact pending tickets. Explicit owner cleanup remains possible; a caller scope is not an OS ownership or exit proof.

Cancellation is not completion. A held CPU flight stays pending/cancellation-requested until actual result consumption or join; joining it does not clear externally owned Market/RetestReview tickets. Actual external settlement after stop still records Unreaped and retains its fact. No current view fabricates reaping or drops unknown external ownership.

## Reproduction and verification

Before the fix, ordinary and ASan/UBSan runs reproduce six of seven scenarios: stale cached results after native fault/intent stop, stale-caller cancellation of a healthy owner, allocation-refused current views, and two held CPU cases. Stale-start refusal already passes as a control. The held native-fault case also demonstrates that a thread without the original scope can admit a nested synthetic native task against a cleared newer epoch.

`test_economic_native_lifetime` uses the actual scheduler, immutable fixture input, CPU study builder and owned thread. Its separately compiled calculation seam supplies a bounded barrier and deliberately ignores cancellation in held cases. No helper, process discovery, source, device, network or worker command is executed. The C++ `new` replacement is linked only into this fixture. In the completed-result allocation case all threads reject `new` during the getter checks; the checks require zero attempts. This is not arbitrary allocator interception, physical-memory-exhaustion or hard allocator-latency qualification.

Seven CTest cases (`economic_native_*`) cover native fault, explicit intent stop, foreign caller, allocation refusal, stale start and held native/intent revocation. Linux uses strict direct GCC and separately instrumented own code with ASan/UBSan/leak detection, not Linux CMake. Whole-host, economic consumer and repetition results are recorded in the execution ledger after they complete. Ordinary artifact guards must still exclude the calculation seam.

The isolated default-off Windows/MSVC full suite passes **356/356** (all CANARY boolean opt-ins OFF), and fifteen affected economic/native/artifact cases pass five further repetitions. Linux ordinary and own-code ASan/UBSan/leak-detection modes each freshly rebuild **38** own fleet units, pass full session/economic/runtime **134/336/78**, cached ceiling **12** five times and pending withdrawal **70**. Seventeen economic/measurement suites pass per mode, including evaluation **149/215**, guard **754/814**, command **467/535**, measurement **25203** and experiment budget **4718**; all seven lifetime cases pass five further repetitions and all seven ordinary artifact-isolation assertions pass. Three shipping/test source hashes remain unchanged throughout each run. External native libraries remain uninstrumented; these counts do not imply real source/hardware/release qualification.

This is CPU/reference ownership, not actual source qualification, measurement, game experience, economic actuation, hard native bounds, deployment or release qualification. Enclosing/remaining owners and the full roadmap remain open.
