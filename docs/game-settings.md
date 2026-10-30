# Native game defaults and startup refusal (P-11h)

M-GAME/M-UI; R-GAME-001/004/006, R-UI-001/007/008, R-SAFE-001/002 and R-PRIV-002; T-GAME/T-UI/T-INTENT/T-PRIVACY; D-69.

Open **S → Games → Game mode / exact target defaults**, or use `canary-miner settings [--config FILE]` while idle. The page edits a separate private `game-preferences.json` in the selected state directory. It exposes a next-parent request; it does not qualify a profile or start detection.

## Six explicit fields

- **Mode:** `off` (default), `pause`, or `coexist`. Coexistence still needs a comparable real-game baseline, experience limits and qualified economic evidence. Saving this choice cannot supply them.
- **Target:** one exact ASCII process basename, 1..128 letters/digits/dots/dashes/underscores; `.` and `..`, paths, patterns, command lines and Unicode names are rejected. Empty is allowed only when Off. No executable search, path import or launcher-to-game inference occurs.
- **Scope:** explicit Windows session image or Linux visible native-UID image. Unsupported/foreign scopes and incomplete visibility cannot prove game exit. The missing-file default chooses the native adapter without scanning.
- **Poll:** 100..10000 ms, independently configurable from evidence age and debounce.
- **Exit debounce:** 0..30000 ms; zero still requires a fresh complete absence observation.
- **Maximum detection age:** 100..30000 ms. A poll slower than freshness may deliberately hold work; no setting is silently increased to make evidence appear valid.

Pure conversion supplies the fixed logical key `selected.game`, the existing detection policy and guard policy. Helper timeout, scan/process capacity and worker leases retain their independent limits. Coexistence qualification remains Unsupported and the ten-minute nonpositive-profit rule remains enabled. No basename is used as public game identity.

## Editing, review and activation

Tab/arrows select a field. Space/Enter cycles mode/scope; Enter opens a target or timing editor. Q/D/L/S are text inside editors; Esc cancels that field and Ctrl+C stops. V/F2 opens review; Enter/F2 explicitly saves. Esc returns to editing, and a dirty leave offers review, discard or continued editing. Quit never implicitly saves.

Review contains every before/after field, the selected save target, next-explicit-parent scope and the separately supplied current-session frozen fields. Arrows/Page Up/Page Down scroll the entire review, including long targets and its final marker. Small terminals retain focused fields and the editing suffix; fewer than twelve rows request resizing. Games, presentation and other runtime drafts save independently. Save success does not modify the current session, worker retries, detector target or game episode.

**Ordinary parent startup currently refuses any non-Off request with configuration exit 3 before device discovery.** It also refuses malformed, unsupported or unreadable game preferences while preserving the file. This prevents an unsupported requested protection from silently becoming unrestricted mining. Select and save Off to permit an ordinary start. There is no new `--no-game` bypass. Idle settings remains available; children do not reread mutable parent defaults, and research/benchmark paths retain their independent admission.

The supervisor receives the original frozen context plus a private store path, and only the explicit Games page loads later saved defaults. Opening/rendering/saving never constructs a game helper or a worker fleet. The ordinary fleet factory and qualified safety source remain unbound.

## Local storage

The [v1 schema](../config/game-preferences-v1.schema.json) has exactly seven required fields, strict enum/integer ranges and a 4-KiB document limit. Missing files supply Off without writing. Failed load leaves the caller's value unchanged and revokes that store object's save eligibility. Errors describe the field/rule without quoting private input or filesystem paths.

Save validates the full draft, takes a separate OS-backed `.lock` lease and compares presence plus original bytes before writing. Competing cooperative writers cannot overwrite a stale review. It writes a same-directory exclusive temporary, flushes it, then replaces the target; Linux additionally fsyncs the directory. Directory-sync uncertainty requires reopening/review rather than claiming success. Files, locks and temporaries are ignored by Git. Regular-file checks reject observed symlink/non-file targets; these checks and advisory leases are not protection against an adversarial same-user filesystem race.

This is the existing small synchronous settings-storage model. Physical disk/kernel stalls, private-directory ACL deployment, isolated persistence and bounded interactive-stop latency remain release work. Real game tests, frame metrics, GPU pause timing, source-qualified coexistence and production activation remain separate.

## Verification

CPU tests cover strict parsing and unchanged-on-error output, independent stores/leases, external byte changes, failed-load save revocation, contextual input, before/after versus frozen review, save failure/discard, narrow-terminal focus and complete review. Native host stubs abort on any device setup, allowing ordinary startup refusal to be checked before discovery. POSIX PTY tests exercise the real settings controller and CPU supervisor with synthetic targets only. Exact final results are recorded in the [execution ledger](architecture/execution-plan.md).

The first native/sanitizer run failed a substring assertion because the visible startup-refusal sentence wrapped between “request” and “refuses”. Captured synthetic output confirmed the full text; the key refusal now occupies its own line, and review also lists all current frozen fields. No actual game or production miner is involved in these tests.

Final Windows focused CTest passes **11/11**, followed by **7/7** helper/CLI/schema/document cases after final help/schema changes. Windows/Linux settings pass **313/314** checks; the extra Linux check rejects a symlink. Both platforms pass **13** CLI admission/preservation checks. Linux ASan+UBSan with leak detection passes **314**, Games PTY **12** and existing runtime/settings PTY pass. Rebuilt detector/native/lifecycle checks pass **90/8/35** on Windows and **90/24/35** in Linux isolated scope; session/runtime **120/67**, old settings/runtime **155/235**, and old supervisor **275/260** also pass. Schemas **9/9**, architecture **4/4**, import and native-recipe guards pass. Linux uses direct GCC rather than a CMake/production CUDA qualification.
