# Session intent and launcher lifecycle: P-03a

This is a host-side safety slice, not the completed automatic optimizer/game/runtime-settings system.

## Implemented

- A `SessionIntent` begins inactive. Only an explicit start issues a nonzero session token. Stop revokes that token; a later explicit start creates a different epoch, so old asynchronous results cannot acquire current permission. The contract has one owning thread; it does not claim cross-thread synchronization for arbitrary callers.
- The supervisor checks an already-set stop before touching history, tuning hooks or children, rechecks it before every first spawn/retry and revokes intent before draining. Existing bounded worker shutdown and crash budgets remain.
- Worker guard-budget exhaustion and supervisor-budget exhaustion now return **8, fault latched**, rather than a generic retryable runtime error. A supervised child returning 8 is not restarted by its parent. An explicit fresh run is required.
- A forced supervisor drain returns **7**, matching the existing forced-shutdown code. Clean intentional stop returns **0**. A stop racing a run-loop runtime error remains an intentional stop, not permission to restart another donation/user lane.
- Checked-in advanced launchers retry **only exit code 1**, at most `MAX_RESTARTS` times (default 3, valid range 0–99). `RESTART=0` disables this. Clean stops, forced stops, latched faults, configuration/device errors and signal-style statuses are not retried. A POSIX launcher forwards termination to its currently owned child; termination during backoff cannot launch another miner. Pass-through launchers remain unchanged.

Ship the updated launcher and miner together: an older unconditional external restart loop cannot understand the new safety contract. This development change does not modify any user's installed service, scheduled task, private controller or existing miner.

## Policy contract, not enabled automation

`arbitrateMining` is a pure decision function with priority:

```text
no current authorized intent > latched fault > thermal/critical telemetry
  > game protection > missing/nonpositive enabled economics > qualified mining
```

Game linkage is independently off/pause/coexist. Opted-in game control with an unavailable detector fails closed to a pause, not normal mining; detected coexistence requires a qualified profile. Opted-in economics without valid data or a positive eligible candidate pauses. Disabled economics/detection do not create a dependency on unimplemented providers.

These game/economic constraints are **unit-tested contracts only**. The running supervisor currently uses the session authorization portion, not an invented game detector, economic selector or thermal policy. Hardware tuning remains disabled as before. The subsequent [P-03b host-wait slice](work-wait.md) adds a manual CLI wait. Runtime setting activation/override review and later policy integrations are still P-03 work; no settings or provider callback starts mining.

## Validation

All processes, input values and state are synthetic; launcher tests run copied scripts with an inert native fixture, not a real miner. The Windows test copy suppresses the inherited HKCU setup line, so the test changes no registry setting. Retry delays are replaced only in ordinary test copies; separate POSIX signal tests exercise the real backoff.

- Windows MSVC host/CLI build passed. Product + launcher lifecycle + architecture CTest selection: **18/18 passed**.
- Pure intent/policy test: **785 checks, 0 failures** on Windows and Linux/WSL, including 768 stopped-state combinations and rejection of old tokens after a fresh start.
- Linux fake-worker supervisor: **210 checks, 0 failures**, including already-requested stop without tuning/spawn, fault-latched child, forced exit 7 and exhausted retry budget exit 8.
- Linux fee/session regression: **111 checks, 0 failures**, including no initial lane after stop and a stop/error race without restart.
- Native Windows launcher lifecycle executes 12 synthetic exit statuses and checks one start except for four bounded attempts on exit 1. POSIX lifecycle adds termination during a running fixture and during retry backoff. Both pass; Windows console-event injection is not claimed by the POSIX test.
- Existing launcher tests: Windows 15 tests passed with 4 POSIX-only skips; Linux 15 passed. The affected PTY settings/history scenarios were rerun and passed.
- Full Linux/WSL Python discovery: **112 tests, OK with 3 skips** (the launcher lifecycle class requires the separate native-fixture invocation above). Native Windows Python discovery was also attempted and **did not pass**: 5 setup/import errors from unavailable GNU `g++` and existing default-GBK source decoding. The passing MSVC/selected native CTest results do not qualify that full Windows Python suite.

Representative commands:

```text
ctest --test-dir build/settings-check/windows --output-on-failure --no-tests=error -R '^(product_|launcher_lifecycle|architecture_contract)'
g++ -std=c++17 -Wall -Wextra -Werror -pthread tests/test_product_intent.cpp -o build/settings-check/product_intent_linux
python3 -B tests/test_launcher_lifecycle.py build/settings-check/product_intent_linux
python3 -B tests/test_launchers.py
```

Unrun: production CUDA/protocol/verifier/SASS gates, native Linux GPU/game tests, real Windows console close/Ctrl+C injection across a launcher, installed service/controller integration and long-run qualification. No real automatic economics, game performance qualification, GPU tuning, driver reset or reboot was performed.
