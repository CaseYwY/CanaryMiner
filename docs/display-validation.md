# Display batch: validation evidence

This is a host-side development batch. All financial inputs, devices, worker processes and local preference files used in tests are synthetic. No private mining/revenue/tariff dataset is included.

This evidence describes the earlier display batch. The subsequent local accepted-history implementation has separate [history evidence](history-validation.md); it does not make these financial tests live-income qualification.

| Executed check | Result |
|---|---|
| Windows MSVC CPU targets and CLI stub/preview build | Passed; existing optimization-flag override warnings remain |
| Windows product regression selection (`ctest ... -R '^product_'`) | 14/14 passed |
| Windows display/valuation unit program, final focused run | 77 checks, 0 failures |
| Windows settings/storage/migration unit program, final focused run | 140 checks, 0 failures |
| Linux `-std=c++17 -Wall -Wextra -Werror` display and settings builds/runs | 77 and 140 checks, 0 failures |
| Linux existing supervisor regression, fake workers only | 206 checks, 0 failures |
| Linux CPU CLI stub plus POSIX PTY integration | Redirected rejection; idle save/discard/signal; supervisor S/save/Esc/Q; saved D/L disable and rejected shortcuts passed |

Linux host checks ran inside WSL; they do not qualify native Linux desktop gaming or GPU kernels. The PTY runner creates isolated temporary files, invokes fake test workers and never starts real mining.

Representative commands, from the repository root in the appropriate configured toolchain:

```text
ctest --test-dir build/settings-check/windows --output-on-failure --no-tests=error -R '^product_'
g++ -std=c++17 -Wall -Wextra -Werror -pthread tests/test_product_display.cpp -o build/settings-check/product_display_linux
g++ -std=c++17 -Wall -Wextra -Werror -pthread tests/test_product_settings.cpp -o build/settings-check/product_settings_linux
python3 -B tests/test_settings_terminal.py build/settings-check/canary_cli_host_linux build/settings-check/product_supervisor_linux
```

Checks cover latest-price revaluation without token mutation, non-peg USDT conversion, unavailable/stale/partial data, absent-versus-zero/negative electricity, cost-horizon matching and distinct energy bases. UI cases cover page/field filtering, structured severity versus unclassified raw lines, limits/timestamps, freeze/follow, review scrolling, active-field visibility and v1 migration/invalid-backup refusal.

Live provider/credit attribution, durable accepted history, tariff/energy computation, native game tests, production GPU proofs and full pinned-toolchain/verifier/protocol/SASS gates are **not** implemented or certified by these tests. Real-time figures must not be advertised until those integrations supply validated data.
