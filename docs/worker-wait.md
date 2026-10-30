# Boundary-applied host-wait receipts (P-03h/P-10e)

M-SUPERVISOR/M-OPTIMIZER; R-PRO-003, R-OPT-003 and R-SAFE-001/002; T-INTENT/T-OPTIMIZER; D-77.

The existing [worker control channel](worker-control.md) now carries an optional explicit **0–1000-ms wait after a complete work batch**. A separate receipt proves installation at the shared CPU-side work boundary. It does not prove GPU work started, a wait finished, a throughput sample completed or a profitable candidate was selected.

## Wire contract and ownership

Legacy commands and receipts retain their exact `CMWC1`/`CMWA1` encoding and behavior. A Run command with `hostWaitMs` uses `CMWC2`; `CMWA2` echoes the entire command, wait and original receipt timestamp. The additional canonical unsigned decimal wait follows the Run directive. Receipt kind `A` is Accepted and `W` is WaitApplied. Zero is an explicit controlled setting; it is not an omitted value. Pause remains v1 and may not contain a wait. Unknown versions/kinds, extra fields, noncanonical numbers and values above 1000 reject. The existing 384-byte frame, 4096-byte input, 16-frame and 5000-ms lease bounds remain.

All existing parent/worker private keys, epoch, slot, incarnation, contiguous revision, issued time and original exclusive deadline still bind the setting. Initial drained Pause remains mandatory. Once a lifetime accepts/issues a controlled-wait Run, later Runs must explicitly carry their wait; a legacy Run cannot silently downgrade that lifetime. Pause continues to be available. Returning to zero uses an explicit v2 Run with zero. A new worker starts with its own independently fenced lifetime.

`WorkerControlIssuer::issue` retains its one outstanding command. A v2 Accepted receipt leaves that command pending; only the exact matching WaitApplied receipt settles it. If Accepted was delivered, its timestamp cannot change on replay and the boundary receipt cannot precede it; a directly delivered boundary receipt also remains valid. `appliedWait(now)` returns the original complete receipt only while the issuer is active, settled and within the original lease. A new request immediately removes it from current use. Changed wait/revision/incarnation/time, late arrival, clock failure or stop cannot grant current application evidence. A matching late receipt can settle bookkeeping after revocation, just as a late Paused receipt can; neither proves process exit or renews authority.

An outstanding command is not overwritten with a later Pause. An enclosing safety owner that must revoke before application/settlement uses the existing stop/channel-revocation path rather than inventing a second outstanding command. Stop never needs a free command slot. Candidate/fleet integration must preserve this ownership rule.

## Worker-thread application

`RunPauseControl::applyWorkWait` is a bounded in-memory worker-thread hook. The shared run loop owns one local effective wait for its lifetime, initialized from the frozen run options. Only the reconciled boundary calls this hook. `WorkerControlGate` serializes the command, checks the original lease and refuses application while held, stopped, expired or faulted. It installs the requested value before producing WaitApplied. Repeated boundary calls and duplicate commands preserve the first application time. The native `WorkerControlClient` forwards the hook to that same gate; its I/O thread cannot manufacture application evidence.

An initial controlled setting is installed after initial pause release and before run-loop configuration/source setup. Device discovery in the separately admitted child entry may precede this run-loop boundary; the receipt is not an earlier device-setup guarantee. Subsequent settings apply only after the previous batch's search, proof handling and loop accounting. Controlled runs disable lookahead, and a delay also checks lookahead drain. Neither work/proofs nor a search are split to meet a setting timestamp.

Pause handling/application occurs before the post-batch delay. The delay checks stop and control revocation in the existing small host-wait slices; a new Pause can reach the drained pause boundary without waiting out the old delay. The loop checks the boundary again before fetching fresh work. A new Run setting arriving during a delay applies at that next boundary; its timestamp does not retroactively label the elapsed delay. Requested, accepted, installed and subsequently observed work remain distinct.

The existing environment/ordinary-run admission restrictions still govern controlled runs. This mechanism changes host-side waiting, not clocks, power limits, kernel math, wallet/pool configuration or developer-donation rules. Pause/accounting and fresh-job-after-wait behavior remain part of the shared loop. The default-off managed-worker entry and empty ordinary supervisor factory remain the activation boundary.

## Verification

The protocol suite pins unchanged v1 bytes, literal v2 encodings, zero/max waits, every command-frame split, malformed fields, Accepted-versus-WaitApplied, a retained pending slot, exact original time, heartbeat/replay/substitution, downgrade refusal, stop/clock/expiry and late bookkeeping. The actual shared-loop CPU fixture requests a different wait during search and verifies that its receipt remains Accepted through proof submission, then becomes WaitApplied at the next boundary. An owned native subprocess uses the real channel/client/shared loop to settle controlled 1000-ms and zero settings with the parent issuer, including pause between them. No native control frame is emitted to the fixture's log stream.

Windows focused invocation (after building the matching targets):

```text
ctest --test-dir build/credits-check/windows -R "^(worker_process|worker_process_lifecycle|worker_control|worker_control_pipe|work_pause|run_work_pause|work_pause_artifact|game_worker_session|product_fee|product_fee_session|product_intent|product_throttle|test_pearl_run_session|throttle_cli|architecture_contract|native_build_recipe)$" --output-on-failure
```

Linux uses direct GCC with `-std=c++17 -Isrc -O1 -Wall -Wextra -Werror -pthread`, rebuilding the worker control/process/owner/identity, local-task/entropy, game-session/runtime and energy-owner objects plus the shared run-loop fixtures. The sanitizer selection adds `-g -fsanitize=address,undefined -fno-omit-frame-pointer` and leak detection without suppressions. Unchanged supporting arithmetic/source objects are reused. This is CPU/IPC evidence; it is not a Linux CMake or production CUDA build.

Final Windows focused CTest passes **16/16**. Windows/Linux ordinary and Linux ASan+UBSan/LSan each pass protocol **818**, private-pipe **14**, pause helper/shared-loop **31/60**, owned native IPC **132** and game-owned session **126** checks, plus lifecycle, source-admission/artifact and CLI guards where selected. Windows also rebuilds/runs fee scheduler/session **6122/127**, intent **785**, throttle **48** and Pearl run-session host regressions. Architecture/import checks pass. Initial Windows/Linux runs passed protocol 796 and shared-loop 60; final review added original Accepted-to-boundary timestamp ordering and made native check counts independent of whether transport coalesces intermediate Accepted delivery. The initial sanitizer build exceeded its 120-second wrapper deadline before producing test evidence; the complete final selection passes with a 600-second wrapper budget. No timed-out run counts as a pass.

## Remaining integration

The economic owner still exposes synthetic reference studies. Candidate hysteresis, switching budgets, binding an exact decision/profile/context to this receipt, native optimization controls and qualified measurement/economic inputs remain. A WaitApplied receipt alone cannot select a candidate or authorize mining. It supplies the actual boundary event that future transition/cooldown and measurement owners must use, instead of treating command issue or Accepted as application. Physical GPU/game behavior, long-run timing and release/protocol/SASS gates remain open.
