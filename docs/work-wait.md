# Complete-work-batch host wait: P-03b

`--work-wait-ms N` requests **0–1000 milliseconds between completed work batches** in ordinary Pearl pool mining. Default **0** preserves the existing schedule. This is not duty cycle, a hardware clock/power limit, a guarantee of profitability, or a calibrated game profile.

## Boundary and safety

The run loop waits only after a search's proofs have been reconciled and its counted work recorded. It never sleeps midway through a kernel/proof operation. Fee lane boundaries retain their original ownership and scheduler logic. The first search has no artificial startup wait; reconnect/setup time is not a new completed work batch.

With a positive wait, next-attempt hinting is disabled and owned lookahead is discarded before waiting so GPU lookahead work does not intentionally overlap a host-idle interval. After waiting, the loop fetches fresh pool work before preparing/searching again. It does not repeatedly compute the previous job or replay a share. Reported average rate still divides actual counted work by real elapsed time, including waits.

The wait uses one monotonic deadline and checks the existing stop flag in requested slices of at most 10 ms. Zero requests no clock read or sleep from the helper. Invalid duration, clock rollback or deadline overflow fails closed; oversleep does not renew the original deadline. OS scheduling can delay wake-up: 10 ms is the **requested polling slice**, not a hard real-time guarantee or a new GPU-interruption bound. The supervisor's existing worker/stop/safety processing remains separate.

Positive waits are rejected in benchmark, formal, quota, sustained, private/public evidence/guard and selected experimental pipeline/profiling modes, before device/source setup. This avoids silently changing qualified evidence runs. No disabled GPU tuning facility is enabled. The same run option is copied into a donation lane; no donation destination, fee percentage, protocol bytes or accounting class is changed.

## Configuration scope

The initial slice exposes a validated CLI option. Subsequent [P-03c native defaults](runtime-settings.md) adds menu editing, persistence and configured/effective/CLI review, with resolved child options frozen for the current session. Live activation remains unfinished P-03 work. Do not add the field to an older strict mining-config JSON schema or mistake UI refresh for workload throttling. Rollback is to use a zero saved default and omit the flag, or explicitly select `0`; an older binary rejects an unknown flag and ignores the new defaults file.

## Validation scope

- Synthetic monotonic-clock helper and admission tests on Windows/MSVC and Linux/WSL: **48 checks, 0 failures each**.
- CLI-host tests reject missing/malformed/negative/oversized/duplicate values, accept explicit zero with help, refuse positive benchmark waits and refuse research/pipeline selectors. They start no actual mining.
- The Linux host admission test compiles the **actual `run.cpp`** and uses aborting device/network wrappers. It verifies out-of-range and benchmark combinations plus nine environment-selector conflicts refuse before algorithm configuration, telemetry or sockets. Result: `Pearl host admission PASS; no GPU/network/accepted-proof evidence`.
- Windows product, launcher lifecycle, throttle CLI and architecture CTest selection: **20/20 passed**. Linux/WSL throttle CLI: **3 tests passed**; settings/history PTY passed. Full Linux/WSL Python discovery: **115 tests, OK with 6 skips**, because fixture-dependent lifecycle/CLI tests require the separate native-executable invocations. The native Windows full Python discovery limitation remains recorded in [P-03a](session-intent.md); it is not a passing native full suite.

These checks do **not** measure real GPU power reduction, actual elapsed wait accuracy under load, native Windows production `run.cpp`/CUDA qualification, game performance or positive net return. The actual GPU search/wait boundary still needs the normal authorized production/protocol/SASS/verifier and long-run gates; no real miner was launched or replaced for this slice. There is no automatic economics/game control, measured curve or positive-profit claim.
