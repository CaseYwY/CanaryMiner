# Optional desktop controls: development roadmap

This fork is an incremental development branch, not a replacement release or a claim of upstream acceptance. No upstream issue or pull request has been submitted for this roadmap.

The detailed [architecture book](architecture/README.md) now owns stable requirements, final valuation semantics, module/task/test traceability and current status. The first-slice description below remains historical evidence; subsequent Details/Log controls and the financial presentation contract are documented in [display settings](display-settings.md).

## Implemented in the first slice

- Native settings state machine and renderer, entered with **S** in the supervised terminal dashboard.
- **Arrows** select, **Tab** changes pane, **Space/Enter** changes a supported value, **V/F2** reviews changes, **Enter/F2** saves, **Esc** returns. Dirty navigation offers review, discard or continue; Q/Ctrl+C stops rather than saving implicitly.
- Live presentation preferences: display-refresh interval, terminal color, recent events and hiding the worker label. Color still respects `NO_COLOR`.
- Separate, local, versioned `ui-preferences.json` in the configured state directory; opening settings does not create a file or rewrite mining config. Atomic replacement and stale-writer detection preserve existing values on failure. A lock directory left by a process crash requires user inspection/removal before retry; it is not automatically discarded.
- `canary-miner settings [--config FILE]` opens the same menu without starting mining or requiring a wallet/GPU. Redirected/noninteractive use is rejected.
- The display-refresh setting does not change telemetry cadence or mining intensity. CLI and noninteractive workflows remain intact.

For a development-only native preview, configure the existing CPU-only `tests` CMake preset and build target `canary_settings_preview`. Run it in an interactive terminal; `--snapshot` prints a synthetic, noninteractive frame. This target contains no mining entry point. It is not a production miner binary.

Hiding a worker label is presentation-only, **not** comprehensive log/API/config anonymization. Settings do not suppress donation, change pools, tune hardware or alter kernel/proof mathematics.

The presentation-file schema is [ui-preferences-v1](../config/ui-preferences-v1.schema.json). Existing defaults are preserved.

## First-slice validation scope

This is a host-side development slice. Native settings/state/storage tests, the existing product regressions, CPU CLI stubs,
and a Linux PTY test cover menu save/cancel/signal/noninteractive paths without starting mining. Windows uses native MSVC
builds; Linux host tests can run under WSL. Passing WSL host tests does not qualify native Linux gaming or GPU operation.

The upstream Windows handle-isolation regression previously tested whether a numeric handle existed in the child. Handle
values are process-local and can be reused for an unrelated object, causing intermittent false failures. The test now checks
whether the **actual parent event** can be signaled, with unrestricted inheritance as a positive control. Production
child-process inheritance is unchanged. Dashboard golden fixtures use LF on both operating systems to match renderer bytes.

The full pinned-toolchain `tools/test_all.sh`, CUDA kernel SASS equality, production GPU proofs and real-pool/long-duration
tests remain mandatory before an upstream production release; this slice does not claim those gates have passed.

See [host validation evidence and its limits](settings-validation.md).

## Planned, not implemented

The other settings groups are explicitly read-only/unavailable until their backend and tests exist. They are not working feature switches.

| Slice | Deliverable | Required boundary |
|---|---|---|
| A | Terminal settings and local presentation persistence | No mining-behaviour change |
| B | Verified Stratum TLS | Default TLS preference, explicit per-pool plaintext consent; verification errors never downgrade |
| C | User intent, bounded host-side throttle and policy state | Intentional stop never resumes automatically; unchanged kernel mathematics |
| D | International tariff engine | Currency/timezone/billing periods; flat, progressive, time-of-use and dynamic costs; explicit unsupported-charge warnings |
| E | Replaceable market/FX/reward adapters | Asset identity, bounded HTTPS, freshness, provenance, manual/offline input; no trading or wallet keys |
| F | Bounded local energy/performance profiling | Per-device/software fingerprints, warm-up/repeated measurement, test energy/time budget, drift invalidation |
| G | Profitability selector and schedules | Best measured eligible candidate per elapsed time, hysteresis, uncertainty and clear power basis |
| H | Game detection and pause | Off/pause/coexist policy, launchers/games/manual trigger, debounce; no game injection |
| I | Tested game coexistence | Comparable actual-game baseline, permitted FPS/frame-time metrics, incremental energy, safe fallback; no full-speed sweep during play |
| J | Shared localization infrastructure | Invariant CLI/API keys, cell-width/grapheme/RTL handling and English fallback |
| K | First internationalization locale packs | en, zh-Hans, zh-Hant, es, pt-BR, fr, de, ru, ja, ko, ar, hi, bn, id, tr, vi; review status recorded |
| L | Shareable diagnostics and release validation | Local-only by default; explicit export preview, redaction and both OS test matrices |

Each slice should be independently reviewable and revertible. Language packs may use multiple PRs but belong to the first internationalization milestone. No dates or universal-performance guarantees are implied.

## Non-negotiable constraints

1. Optimize estimated net return per real elapsed time under user/safety/game limits, not raw hashrate or efficiency alone. Call results best among measured candidates, not a global optimum.
2. Distinguish telemetry interval, calibration warm-up/duration, price lookback, provider refresh, policy evaluation and recalibration. Respect provider request limits.
3. Distinguish GPU-board proxy, whole-PC estimate and measured incremental wall-energy. Missing game/energy evidence is unverified, not guaranteed positive profit.
4. No personal defaults or imported deployment data. Tariff/location/provider selection is explicit, not inferred from language or IP.
5. Existing disabled hardware tuning stays disabled. Any future reintroduction is a separate opt-in with a restricted helper and restoration on stop/crash; miner-side throttling comes first.
6. Preserve optional developer-donation settings, destination, scheduling, proofs, protocol bytes and accepted/rejected accounting.
7. Never reboot, reset a display driver, kill a game, bypass anti-cheat, acquire wallet keys, trade, upload telemetry, or start at login as an implicit feature side effect.
8. Windows and Linux share the policy core but report actual capabilities; WSL is not automatically a supported gaming desktop.
9. Default game coexistence without evidence is pause/safe fallback, not an untested full-rate profile. A launcher-only run is detection evidence, not FPS validation.
10. Before upstream behaviour/protocol contributions, follow CONTRIBUTING.md's issue-first, DCO, full-gate and honest-evidence requirements. A fork branch with host tests is not a fully qualified production release.
