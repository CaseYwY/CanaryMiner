# Accepted-history batch: validation evidence

Development scope: CPU-only synthetic counters/times/files, isolated child processes and loopback fake-worker stats. No production miner, GPU control, real pool account, wallet, credit or tariff data was used. Existing installed mining software was not modified.

## Executed checks

- Windows MSVC host build, CLI stub and settings preview: passed (existing optimization-override warnings remain).
- Windows history unit program and Linux `-Wall -Wextra -Werror` history program: **290 checks, 0 failures each**.
- Windows product regression plus architecture contract: **16/16 passed**.
- Linux existing fake-worker supervisor regression: **206 checks, 0 failures**.
- Linux POSIX PTY: idle settings save/discard/signal, S/review/save/Esc/Q, disabled D/L shortcuts, two independent supervisor sessions and SQLite inspection passed. Each fake worker exposes 2 accepted user shares plus 999 donor shares, rejected/pending values; two sessions persist **4**, not the donor or pending counts. Tests use no external network.

History cases cover multi-slot aggregation, same-generation deltas, duplicate/stale/reset observations, checked overflow, 24h/7d/30d exact second/minute boundaries, a direct rolling-window oracle, retention preserving lifetime totals, explicit interval coverage, clock warning, committed crash recovery and uncommitted rollback. They also check exclusive leases, competing SQLite writers, unknown/newer/truncated files preserved without reset, Unicode filenames, background queue/drain, strict missing/invalid counters and dashboard/plain unavailable labels.

Representative commands from the repository root with the applicable toolchain:

```text
ctest --test-dir build/settings-check/windows --output-on-failure --no-tests=error -R '^(product_|architecture_contract)'
g++ -std=c++17 -Wall -Wextra -Werror -pthread tests/test_product_history.cpp -ldl -o build/settings-check/product_history_linux
build/settings-check/product_history_linux
python3 -B tests/test_settings_terminal.py build/settings-check/canary_cli_host_linux build/settings-check/product_supervisor_linux
```

Linux checks ran under WSL, not native Linux GPU/game qualification. No complete fixed-toolchain/verifier/protocol/SASS/production-CUDA/long-run gate, physical power-loss test, network-filesystem qualification, slow/full-device fault campaign or clean-machine runtime packaging test was executed. Missing-library and queue-overflow behavior are implemented fail-closed paths, **not independently fault-injection-qualified claims**. Native runtime settings controls, historical repair/export and actual-credit/market/electricity integrations remain separate tasks. Financial display remains disabled without providers.
