# Revoke an uncovered pending Run (P-03i/P-11n)

M-SUPERVISOR/M-GAME; R-SAFE-001/002 and R-GAME-004/005; T-INTENT/T-GAME; D-80.

The actual `GameWorkerSession` now revokes its owned channels when an outstanding Run is no longer covered by current safety/game permission. An outstanding command cannot be overwritten with a second Pause. Waiting for a withheld Run receipt would leave the earlier command usable after its permission was withdrawn.

## Actual control path

After admitting received bookkeeping and calculating the current Run deadline, the session checks the exact pending command. If it is Run and either there is no current Run permission or the new deadline is earlier than that command's original deadline, the whole controlled fleet latches Faulted. Every issuer and process owner receives `requestStop`, queued launches are cancelled, and pause-requested/revoked is visible without claiming drained Paused. This is an in-memory control-thread operation; it does not wait for receipt delivery, pipe closure, worker drain or process exit.

The existing native background owner closes/revokes the channel and drains/reaps the child. Command bookkeeping, channel revocation and actual reaping remain distinct. The fault is not a retryable runtime exit, even if an owned CPU child responds to channel closure with exit 0. Later safe observations cannot revive that fleet or authorize a replacement. Intentional parent stop retains its existing Stopped priority.

A pending Pause remains a safe request and does not require a second Pause. A pending Run fully covered by current evidence remains pending. A settled Run still takes the existing cooperative Pause path when policy changes; this batch does not replace full-batch draining with mid-search interruption. No protocol frame, kernel, donation rule, process restart policy or ordinary factory activation is changed.

The actual permission ceiling stays separate from the early-Pause request. The existing delivery headroom (at most 100 ms and half the configured command horizon) may request Pause while the original Run is still fully covered. It cannot turn that valid permission into a zero deadline for pending-command validation. The pending command retains its original lease; genuine missing or shorter permission still revokes, including inside the headroom. Neither source validity nor command expiry is extended.

## Reproduction and verification

The test starts the actual session/identity helper/native process owner with a self-owned CPU protocol peer. That peer sends the exact initial Paused receipt, receives the Run, prints only a phase marker and deliberately withholds its receipt. No work is performed. The parent then supplies an invalid aggregate telemetry observation while the original Run lease is still valid and asserts immediate fleet revocation before that deadline. The initial test failed repeatedly in about 0.2 seconds. Targeted state inspection showed Pause requested, the same command revision and an unrevoked Active fleet: the pending early return, rather than evidence rejection or stale publication, was the cause. The inspection log was removed after the fix.

Expanded cases cover missing telemetry, unsafe thermal state, explicit fault, missing evidence, a still-positive but shortened original-evidence ceiling, safe pending continuation, intentional stop, newly detected synthetic game presence and an unacknowledged initial Pause. Every revoked case preserves the command revision, prohibits revival/relaunch and separately verifies real child exit/reaping through the native channel. Existing settled-Pause, game episode, expiry, retry and supervisor regressions remain selected.

The first full regression found an overly broad fix: converting delivery headroom to a zero Run deadline incorrectly revoked a still-covered pending renewal. Windows passed only 11/12 selected cases and the Linux instrumented session had four failures. The cached-safety case depended on receipt timing, so the receipt-withholding peer now drives the controller exactly 50 ms before its original deadline. It reproduced the defect deterministically (67 checks, one failure) and then passed after preserving the real permission ceiling and deriving Pause separately. The same case verifies genuine thermal withdrawal inside that headroom. Temporary diagnostic logs were removed.

Targeted command after building `test_game_worker_session`, `game_detection_fixture` and `canary_worker_identity_worker`:

```text
ctest --test-dir build/credits-check/windows -R "^game_worker_pending_revocation$" --output-on-failure
```

The full Windows selection includes that case plus game session/runtime, worker process/lifecycle/protocol, pause/shared-loop, supervisor, CLI and architecture/build-recipe tests. Linux directly rebuilds the changed session and synthetic detection/worker fixtures using `-std=c++17 -Isrc -O1 -Wall -Wextra -Werror -pthread`, reusing unchanged supporting objects and regression executables. ASan+UBSan adds `-g -fsanitize=address,undefined -fno-omit-frame-pointer` with leak detection and no suppressions. These are CPU/native-channel tests, not physical kernel/driver stop timing, GPU/game experience or production source qualification.

Final Windows focused CTest passes **12/12**. Windows/Linux ordinary and Linux ASan+UBSan each pass pending revocation **67**, session **126**, runtime **67**, protocol **818**, native IPC **132**, plus owned-parent-death and **9** CLI-refusal lifecycle checks. The separate Windows cached-safety case passes **7** checks. Full final runs supersede the failed intermediate fix; Linux uses direct GCC rather than Linux CMake.
