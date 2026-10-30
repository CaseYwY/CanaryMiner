# Original completed-work source (P-09e)

M-PROFILES/M-SUPERVISOR; R-PRO-001/002/003, R-SAFE-001/002 and R-PRIV-002; T-PROFILES/T-INTENT/T-PRIVACY; D-87.

The shared `runMiner` loop now has an optional borrowed `CompletedWorkSink`. It reports original synchronized and reconciled work batches independently of display/report cadence, accepted shares and financial data. A default-off `CompletedWorkCapture` accumulates exact user and donation TMAC counters across sequential run calls. Ordinary startup leaves the sink null. No CLI/runtime source factory, profile qualification or native sampler is enabled by this change.

## Exact backend capability and boundary

`Algorithm::exactMacsPerUnit` is an explicit integer capability; zero means unsupported. Pearl's host implementation uses the same compiled `tile side × tile side × K` as its existing displayed MAC conversion, without converting a binary floating-point value. No kernel arithmetic, proof bytes, pool policy or donation scheduling is changed.

An opted-in capture requires ordinary cooperative control, which already excludes research/evidence, benchmark and hardware-tuning modes and disables lookahead hinting. The loop records the backend's completed-search count and exact factor before search. After all proofs are reconciled, it requires:

- the backend's synchronized completed-attempt delta equals the full scheduled batch;
- the exact MAC factor and work units per attempt still match;
- the batch has nonzero integral work/attempt counts.

Only then does the sink receive the original batch: actual integer work units/attempts, integer MAC factor, installed host wait, host-verification-failure flag and original UTC/monotonic clocks. The callback precedes the next loop hook, fee switch, wait and report. A stop received during the final search does not discard that fully reconciled batch. No-hit work still counts; accepted/rejected/delivered shares do not determine the work total. Host verification failure invalidates the opted-in source rather than creating useful-work authority.

The loop uses scoped source ownership. Explicit clean closure follows the final lookahead drain or drained fee-switch return. Any early/exceptional exit marks continuity failed. A batch observed before a later drain failure stays a historical original, while the source is faulted and cannot be reused. A source rejection stops that opted-in run with the existing terminal cooperative-control code; no new retry code is introduced.

## Parent counters and lane identity

The accumulator borrows the exact explicit parent intent/token, takes a positive counter epoch and starts once on its worker thread. All methods and reads remain on that thread; it is noncopyable. It has no transport, mutex, background sampler, file or network I/O. The enclosing worker/source coordinator must retain it across sequential user/donation calls and provide any later cross-thread or cross-process handoff.

The fee session explicitly sets `WorkLane::Donation` on its copied options; the outer user loop sets `WorkLane::User`. The same sink pointer survives both copies. Neither account strings, payout destinations nor display labels classify a lane. Fee policy, window durations, debt, watchdogs and retry decisions retain their existing semantics.

Each accepted batch contributes exactly `workUnits × integer MACs per unit × 10^-12` TMAC to its lane. Arithmetic uses `ExactDecimal`; raw values beyond binary64's exact-integer range retain every digit. Totals are bounded to 38 coefficient digits and 12 fractional places, matching the existing work-input contract. Overflow/capacity refuses instead of saturating. Attempts use checked integer addition. The first batch freezes work units per attempt and MAC factor across run calls; a changed factor/shape invalidates continuity.

The independent run-count ceiling is 1–8192. Each original retains its lane, run ordinal, batch ordinal, applied wait and original clocks; beginning another lane never relabels the previous original. Cumulative parent-clock disagreement above 2000 ms, reversal, overlap, malformed batches, unsupported precision, failed closure or parent stop latch refusal. New run calls or a later parent start cannot revive the object.

## Raw work is not a measurement sample

The snapshot retains cumulative counters and the last complete batch. Its `observed` field is the latest original lifecycle event; `lastBatch` keeps its own original time. Reading either is not a new observation, and a previously complete boundary cannot be restamped while the worker is searching again. The source emits no zero-work heartbeat, power reading, safety/game status or drift qualification. Gaps and missing final samples remain explicit work for the sampling coordinator.

No adapter converts these facts into a qualified `WorkProfileSample` yet. Native sample transport, original power/time alignment, complete paused-boundary observations, private device/work/driver identity binding, full safety/game evidence and experiment/fleet admission are still required. `SyntheticFixture` remains the only admitted profile qualification. Raw completed work establishes neither attributable pool earnings nor accepted-share income.

[P-09f](work-power-alignment.md) adds explicit drained/held and runnable-boundary intervals plus conservative original power-support alignment. The batch's historical time remains untouched. Native sample transport, qualified sensor/identity/safety binding and measurement/fleet admission remain separate.

## Verification

The source tests cover exact beyond-binary64 arithmetic, user/donation separation, immutable originals across lane changes, changed shape/factor refusal, parent-clock continuity, incomplete/exceptional endings, overflow, invalid input, stopped-parent refusal and all 8192 run slots. The actual shared-loop CPU test injects an in-memory source and a backend completion counter, deliberately uses an unrelated display MAC value, and verifies reconciliation order, a final stopped batch, no-hit work, clean fee-switch reuse, missing/short/changed completion, host-invalid work, failed drain, sink refusal and exceptional unwinding.

The existing actual issuer/gate shared-loop test also attaches a raw sink: observed waits match the actual boundary-installed sequence, including an explicit zero-wait update, and no unresolved request is substituted for the installed value. Fee-session regression checks explicit lane selection on every simulated run. Ordinary run-loop artifact and admission tests preserve fixture isolation and pre-device/pre-network refusals.

CPU/native-process results are recorded in the execution ledger. These do not qualify a real GPU measurement or a production CUDA artifact. The pinned CUDA 13.1 compiler is unavailable in this environment, so the Pearl host override has not been compiled with the pinned toolchain or verified by SASS/protocol gates in this batch. Integrated physical GPU/game acceptance remains deferred as planned.

Final evidence: Windows **20/20** focused cases plus a final **4/4** rebuild after making the accumulator noncopyable. Windows/Linux ordinary and Linux **ASan+UBSan with leak detection** pass exact source **8,291**, actual loop/source **67**, shared pause/installed wait **61**, fee-session **205**, native CPU IPC **132**, pause helper **31**, donation scheduler **6,122** and throttle **48** checks. Both new source suites were rebuilt again on Linux ordinary/sanitizer after the header-only ownership change. Ordinary loop admission and fixture-isolation guards pass; Windows also covers worker lifecycle/protocol/pipes, measurement/budget/profile, intent, Pearl run-session, host CLI, recipe and architecture. Final architecture/import checks pass. Linux reuses corresponding unchanged support objects/native harnesses and is direct GCC evidence, not Linux CMake. Existing MSVC pool size-conversion and test optimization-override warnings remain visible; no warning policy was weakened.

The final Windows focused commands, from an existing CPU-only MSVC developer environment, were:

```text
cmake --build build/credits-check/windows --parallel 2 --target test_completed_work_capture test_run_completed_work test_run_work_pause test_product_fee_session
ctest --test-dir build/credits-check/windows -R "^(completed_work_capture|run_completed_work|run_work_pause|product_fee_session)$" --output-on-failure
python -B tests/test_architecture_contract.py
python -B tests/test_auxiliary_imports.py
```

Linux uses `-std=c++17 -Isrc -O1 -Wall -Wextra -Werror -pthread`, adding `-g -fsanitize=address,undefined -fno-omit-frame-pointer` for sanitizer execution, `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. Only the CPU fixture loop defines `CANARY_WORK_PAUSE_TESTING=1`; the ordinary artifact is separately compiled without it.
