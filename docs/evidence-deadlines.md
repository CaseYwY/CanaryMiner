# Non-extending cached evidence deadlines (P-11k)

M-SUPERVISOR/M-GAME; R-SAFE-001/002 and R-GAME-005/006; T-INTENT/T-GAME; D-72. This corrects an edge case in [P-11g's evidence-derived worker leases](game-worker-session.md).

## Reproduced failure

The original calculation was `now.monotonic + maximumAge - max(wallAge, monotonicAge)`. Each calculation individually respects the current two-clock age. However, if wall time previously advanced faster and the relative skew subsequently decreases while **both clocks remain nondecreasing**, recomputing can produce a later monotonic deadline for the same immutable evidence. A deadline already reached under the earlier calculation can then appear usable again.

The minimized test drives the actual `GameRuntime` with an owned synthetic detector, a fixed detection revision and a fixed caller upper bound. It feeds a deterministic pair of nondecreasing clock observations: first wall time advances faster, then monotonic time catches up. Before the fix, both “cached deadline cannot grow” and “previously expired deadline cannot revive” assertions fail: **6 checks, 2 failures**, reproduced twice. The same test with a constant clock difference passes **6/6**, ruling out changed source revision and increasing caller bounds. No real game or GPU is involved.

The focused native test commands, after building `test_game_worker_session` and its existing helpers, are:

```text
build/credits-check/windows/tests/test_game_worker_session.exe build/credits-check/windows/canary_worker_identity_worker.exe build/credits-check/windows/tests/game_detection_fixture.exe cached-deadline
build/credits-check/windows/tests/test_game_worker_session.exe build/credits-check/windows/canary_worker_identity_worker.exe build/credits-check/windows/tests/game_detection_fixture.exe cached-deadline-control
```

The first command turns green after the fix. Its regression is also part of the ordinary complete session test, not only the focused selector.

## Original-observation earliest ceiling

`EvidenceDeadline` retains the earliest computed exclusive monotonic ceiling for an original observation. Re-evaluation intersects that ceiling with the newly calculated bound. Zero/expired results cannot regain authority from later cached ticks. Same-revision timestamp or age changes reject; lower revisions cannot replace the current source. Increasing a revision while retaining the same original timestamp **also retains the ceiling**: renumbering cached data is not fresh sampling. A genuinely newer original observation can bind a new deadline under the same fixed age policy.

Session/source identity and clock/lifecycle validity remain caller responsibilities. The helper supplies no token, measurement, source qualification or worker command. `GameRuntime` retains independent detection and profit ceilings; the profit tracker changes episode only after the existing guard has accepted the new episode's evidence. `GameWorkerSession` retains a separate safety ceiling across worker replacements. Final Run leases still intersect these source bounds with the command lease; deliberate stop, game-profit locks, pause receipts and actual process-reaping rules remain authoritative.

This applies the same non-extension rule already present in [P-03e/P-11j's core-temperature limiter](core-thermal-guard.md) to the earlier game/worker path. It does not qualify raw safety data, native game coexistence or real financial inputs.

## Verification scope

The pure arithmetic test checks decreasing skew, exact expiry, timestamp/policy mutation, renumbered originals, future/invalid input, revision rollback, independent source ceilings and overflow, plus a deterministic grid of forward-moving two-clock sequences. The actual runtime regression checks the two originally failing assertions. Full worker-session, runtime, detector and game-guard regressions check compatibility with the retained controller lifetime and owned CPU helpers.

Windows uses CMake/CTest selection `evidence_deadline|game_worker_session|game_runtime|game_detection|game_guard|architecture_contract`. Linux rebuilds the changed runtime/session implementation and relevant tests with direct GCC (`-std=c++17 -Isrc -O1 -Wall -Wextra -Werror -pthread`). The sanitizer variant instruments those units/tests with ASan+UBSan, reuses unchanged instrumented dependencies and enables leak detection without suppressions. Exact final results are recorded in the [execution ledger](architecture/execution-plan.md). This is CPU/control evidence; physical GPU/game timing, full critical-safety source qualification and ordinary fleet activation remain open.

The first ordinary GCC run rejected the optional ceiling representation with `-Werror=maybe-uninitialized`; the representation now uses an explicitly initialized integer plus bound flag without suppressing diagnostics. The first sanitizer wrapper passed expiry/session/runtime, then failed detector checks because the wrapper supplied its native/fixture helper arguments in reverse order; that invocation was corrected for the complete rerun. Interrupted or failed wrappers are not counted as passing selections.

Final Windows focused CTest passes **6/6**: deadline **670**, worker-session/supervisor **126**, runtime **67**, detector **90** and guard **113** checks, plus the architecture contract. The complete ordinary Linux and ASan+UBSan/LSan reruns each pass **670/126/67/88/113** checks with zero failures. Linux's ambient process scope has incomplete native visibility: the detector explicitly skips two complete-presence/exit checks and verifies Unknown instead. Its **88** checks are not a complete-scope qualification. The separate architecture and auxiliary-import guards also pass.
