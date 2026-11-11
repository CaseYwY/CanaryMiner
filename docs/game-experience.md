# Offline paired frame-experience comparison (P-11q)

M-GAME/M-PROFILES; R-GAME-002/003/004; T-GAME/T-PROFILES; D-171.

`compareGameExperience` audits **synthetic, explicitly supplied** paired no-mining and candidate windows. It does not collect frames, scan processes, inject into a game, capture input, launch a game/miner, change hardware or authorize coexistence. Unsupported/native sources cannot acquire qualification by selecting a launcher or passing this arithmetic.

## Explicit comparison contract

The plan supplies a complete private game-build/scene/device/driver/miner/kernel/display/settings/environment/adapter identity, one complete-batch host wait (0..1000 ms), 2..16 repetitions, nonzero warm-up/window/campaign bounds and explicit experience limits. No game, threshold, energy or jurisdiction default is guessed. Every pair has equal measured duration, original monotonic clocks, increasing source sequence, one source session, complete contiguous frame durations, valid/safe observations and peak memory/temperature. All ten identities must match. The no-mining baseline cannot contain mining or a host-wait request; the candidate must match the requested wait.

Each window contains 100..65536 frames; each duration is positive and their exact sum equals the measured elapsed interval. Warm-up cannot overlap a prior window; campaign elapsed includes warm-up, transitions and gaps, not just selected productive time. The fixed offline input ceiling is one hour, **not** a running-test watchdog or physical cancellation guarantee. No automatic experiment is started.

Metrics are explicitly defined:

- Mean FPS is frame count divided by complete elapsed nanoseconds.
- `slowestOnePercentMean` is the inverse mean duration of the slowest **ceil(N/100)** frames. This named convention is not an interchangeable claim about another tool's “1% low”, percentile FPS or input latency.
- P99 duration is ascending nearest rank **ceil(0.99*N)** (1-based); maximum duration is independent.
- Peak memory and temperature are supplied originals, never missing-as-zero or inferred from the frame rate.

Both baseline and candidate must satisfy absolute mean/low FPS floors, maximum/P99 frame-time, memory and temperature ceilings. Each candidate also satisfies paired exact mean/low regression limits in basis points. Products use the existing bounded exact-decimal core; no binary float, rounded displayed FPS, pooled average hiding a bad repetition or statistical confidence claim can admit a trial. Equality is accepted; a one-nanosecond tail violation is not rounded away. `OutsideLimits` returns the complete failed-trial comparison. Missing/malformed/unsupported/cancelled input preserves prior output and requires the caller to check status.

## Intentionally unavailable

Comparison output is mutable reference data, **not** an immutable qualified profile, a source receipt, session/worker permit or an input to ordinary `GameWorkerSession`. The caller retains original inputs separately; this slice supplies no persistence/import/native collector or bridge to game eligibility. Existing native startup still refuses unqualified non-Off game requests. A real adapter needs permitted capture, stable source/scene comparability and all applicable user limits (including any input-latency/VRAM/thermal contract), actual baseline/repetition/drift/energy tests and lifecycle/stop qualification. No human experience or real-game performance is established by fixtures.

The first strict GCC check exposed one **test-plan** inconsistency: maximum frame time was lowered below its P99 ceiling. The coherent fixture was corrected; production validation was not relaxed. Final portable/sanitizer/cross-platform evidence is recorded only after execution in the architecture ledger. R-GAME-002/003 advance from planned to partial, not completed real coexistence.

Final Linux ordinary/own-code ASan+UBSan and Windows/MSVC pass **353 unit checks** plus **200 independent Fraction cases / 2600 exact comparisons**. Both cases pass ten further repetitions on each applicable build; Windows default-off full CTest passes **426/426**. These are CPU fixture arithmetic/linkage checks, not an actual game, capture or coexistence qualification.
