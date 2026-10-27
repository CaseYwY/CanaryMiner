# Settings foundation: host validation

This records development checks, not qualification of a production GPU miner. Fixtures and outputs use synthetic data. No personal runtime configuration, telemetry, tariff or payout information is published.

## Executed checks

| Check | Result |
|---|---|
| Windows MSVC host build: settings, product regressions, CPU CLI stub and native preview | Passed; existing `/O2` versus `/O1` override warnings remain |
| Windows `ctest --test-dir build/settings-check/windows --output-on-failure --no-tests=error -R '^product_'` | 13/13 passed |
| Linux `g++ -std=c++17 -Wall -Wextra -Werror -pthread tests/test_product_settings.cpp -o build/settings-check/product_settings_linux` and execution | 130 checks, 0 failures |
| Linux equivalent host CLI test | 81 checks, 0 failures |
| Linux supervisor regression with fake worker processes | 206 checks, 0 failures |
| Linux `python3 -B tests/test_settings_terminal.py build/settings-check/canary_cli_host_linux build/settings-check/product_supervisor_linux` | 20 checks, 0 failures |
| Windows isolated event-object inheritance test, including a positive control | 50 consecutive runs, 0 failures |

Linux checks ran in WSL using a host C++ compiler, not CUDA or a native Linux gaming desktop. Windows settings/storage tests ran as native Windows executables. The PTY test verifies the standalone command, S-entry from a supervised fake-worker dashboard, save/apply, Esc return, discard, signal stop and noninteractive rejection. No real miner worker is started by these tests.

## Corrected test problems

- Byte-exact dashboard fixtures needed LF independent of Windows checkout conversion. Snapshot changes add the Settings entry and its natural line wrapping; mining data and counters in those fixtures are unchanged.
- The old Windows inheritance probe had intermittent failures (9/30 isolated runs) because it treated a reused numeric handle as an inherited object. Targeted instrumentation confirmed the original parent event remained unsignaled. The corrected test observes that exact parent event, and unrestricted inheritance must signal it as a positive control. No production process-inheritance code changed.

## Remaining release gates

The full pinned-toolchain `tools/test_all.sh`, SASS equality, production CUDA/proof validation, real-pool transport, long-duration mining and actual Windows/Linux desktop game coexistence have **not** been run for this slice. TLS, economic selection, calibration and localization packs are roadmap items, not implemented capabilities of this branch.

Before publication, run the existing secret scanner against the staged tree and available local Git history using a private, untracked deny-list. Never attach that deny-list or raw build/runtime logs. A shallow-history scan only covers locally available commits; it is not a scan of the entire upstream history.
