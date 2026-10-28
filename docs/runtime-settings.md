# Native runtime defaults: P-03c

Open **S → Automatic mining → Host wait / runtime defaults**, or **Privacy → History collection / runtime defaults**. The idle `canary-miner settings [--config FILE]` command provides the same controls without starting mining.

## Controls and honest activation

- **Host wait:** Enter opens an ASCII numeric editor, accepting every integer from 0 through 1000 milliseconds. Zero is valid, not a duty-cycle percentage. Enter applies only the draft; Esc cancels that edit. Backspace is supported. Q/D/L/S typed inside the editor are input, never global page/quit shortcuts; invalid input stays in the editor with an explanation. Ctrl+C still stops.
- **History collection:** Space/Enter toggles the local default; disabled collection preserves all existing history. This is not a presentation filter or a delete operation.
- **V/F2 → review → Enter/F2** explicitly saves runtime defaults. Review names the target, before/after values, scope, CLI precedence and activation. Esc returns; dirty leave offers review/save, discard or continued editing. Q/Ctrl+C does not implicitly save either target.
- Both settings activate on the **next explicitly started supervisor session**, not at a worker crash/retry, reconnect or game exit. Saving never starts/stops/restarts mining, changes clocks, runs a test or logs in to a provider. To stop collection immediately, deliberately stop the current run; use `--no-history` when explicitly starting again.

The panel displays configured/draft values, the **current session's immutable effective control values**, their CLI-versus-startup-default source, and the next-start projection if the same CLI flags are reused. History-on is a request to collect, not proof of backend availability; Main separately reports loading/unavailable/gap status. Explicit `--work-wait-ms 0` overrides a saved positive wait; `--no-history` overrides saved collection-on. Saving a default is allowed while overridden, but does not erase the override or mislabel it effective now. An idle editor states there is no active session; it cannot know flags for an unrelated future launch.

The parent supervisor freezes resolved values for its entire session and passes exactly one resolved wait to each child, including restarted children. Children do not reread newly saved root defaults during an old session. History recording likewise keeps the startup choice until explicit exit/new start. This deliberately conservative activation is a temporary manual-control scope, not completed live policy re-evaluation or automatic optimization.

## Separate atomic targets

Runtime defaults use `state_dir/runtime-preferences.json`, [schema v1](../config/runtime-preferences-v1.schema.json):

```json
{"schema_version":1,"work_wait_ms":0,"history_enabled":true}
```

The file has no wallet, worker, device identity, tariff, API credential or financial fact. Missing files use the compatibility defaults above without writing. Ordinary parent mining runs validate it before device/source setup; malformed, oversized, unknown-key/newer-version files refuse that run instead of silently inventing defaults. Benchmark/evidence paths, status, device listing and idle display settings do not consume these runtime defaults. Invalid runtime files make their editor unavailable, without disabling unrelated display settings.

`ui-preferences.json` remains schema v2 with its existing v1-backup rules. **UI and runtime changes are separate explicit save targets**, not a fictitious multi-file transaction: opening/saving Runtime controls preserves an unsaved UI draft, and runtime Save writes only the named runtime target. Returning to UI review still shows that UI draft and requires its own save. The idle runtime-only test creates no UI preferences or history database.

The runtime store bounds reads to 4 KiB, validates strict types/ranges/keys, compares the current file to its loaded snapshot under a cooperative writer lease, flushes a unique same-directory temporary file and atomically replaces the target. A changed file refuses save and retains the draft. OS-owned leases release when a writer closes/exits; the application does not steal/delete another lock. Newly created POSIX files are mode 0600, Windows inherits directory ACLs. A trusted local user-owned state directory is required; these files are not encrypted. Filesystem/hardware power-loss durability and adversarial uncooperative directory writers are not certified by these host tests.

Rollback: an older binary ignores the new runtime-defaults file but does not implement those saved defaults. Use the old binary's supported explicit flags where applicable, or return to this build. No UI migration, mining-config rewrite, history reset or installed-miner modification occurs.

## Verification and boundaries

Synthetic Windows/MSVC and Linux/WSL state/storage tests cover parsing/defaults, zero CLI precedence, contextual editing, review/save/discard/failure, narrow/short layouts and visible focus, independent save targets, Unicode paths, leases, stale writers, oversized/newer files and diagnostic key redaction.

- Windows and Linux/WSL runtime-settings unit program: **235 checks, 0 failures each**.
- Windows selected product/launcher/throttle CLI/architecture CTest regression: **21/21 passed**, including existing history, donation, supervisor and display tests.
- Linux/WSL full Python discovery: **117 tests, OK with 7 skips**. The seven fixture-dependent launcher/CLI tests require their separate native-executable invocation; they are not silently counted as executed by discovery. Native Windows full Python discovery still has the earlier GNU-toolchain/default-encoding limitations documented in [session intent](session-intent.md).
- Native Windows and Linux/WSL CLI-host throttle/runtime admission: **4 tests passed each**. Runtime schema structural tests: **5 tests passed** in the current combined config/schema file.

The real Linux/WSL POSIX PTY scenario also checks:

1. Idle numeric Q handling, runtime-only save, discard and external-change rejection followed by signal exit, with no UI/history file creation.
2. A fake supervised session keeps recording after saving collection-off; the panel still says current on/CLI wait 0.
3. The next fake session is collection-off and leaves the old DB total unchanged even after saving collection-on.
4. A third explicitly started fake session resumes collection; accepted totals become 4, not donated/pending outcomes. No real miner/GPU or external network is involved.

The actual CLI-host tests additionally prove a stored positive wait is resolved before setup, explicit CLI zero wins, an unknown runtime schema refuses ordinary mining, and settings help/benchmark admission do not interpret the runtime file as their configuration.

Representative commands, from the repository root with the appropriate host toolchain:

```text
ctest --test-dir build/settings-check/windows --output-on-failure --no-tests=error -R '^(product_|launcher_lifecycle|throttle_cli|architecture_contract)'
g++ -std=c++17 -Wall -Wextra -Werror -pthread tests/test_product_runtime_settings.cpp -ldl -o build/settings-check/product_runtime_settings_linux
python3 -B tests/test_runtime_settings_terminal.py build/settings-check/canary_cli_host_linux build/settings-check/product_supervisor_linux
python3 -B tests/test_settings_terminal.py build/settings-check/canary_cli_host_linux build/settings-check/product_supervisor_linux
python3 -B tests/test_throttle_cli.py build/settings-check/canary_cli_host_linux
```

Remaining P-03 work includes safe live activation where appropriate, external **mining-config** change review beyond these local stores, and native GPU/protocol/energy/long-run qualification. This does not implement prices, income, electricity, curve search, game detection, TLS or language packs, and must not be called a qualified production release.
