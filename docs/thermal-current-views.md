# Thermal current-view revocation and stale-caller denial (P-14l)

M-SUPERVISOR; R-SAFE-001/002; T-INTENT/T-RELEASE; D-168.

The pure selected-device thermal limiter now withdraws cached `CoreWithinLimits` / `ExtendedWithinLimits` on loss of its original explicit parent, without waiting for a new temperature update. A stale caller gets a preallocated Stopped view and cannot change a healthy newer limiter's clocks, source uncertainty, cooling state or first-start admission. This adds no sensor, threshold default, complete safety provider, worker permission or ordinary activation.

## Memory-only current versus historical state

Current read revocation clears the passing deadline, cooling count/interval and internal admission flag, then latches Stopped. It retains the original source revision/observation and any previously supplied Unreaped flag as history. Copied prior passing snapshots stay unchanged and cannot renew current authority. The exact earlier source deadline is never refreshed by reading.

Foreign denial precedes source-Unreaped accumulation and timestamp checks in `update`, and precedes first-start bookkeeping. A healthy parent's next earlier-clock update therefore remains valid. Actual stopped callers may still supply late uncertainty, but cannot restart the same limiter or obtain a passing deadline. Explicit owner cleanup remains available.

The const current read mutates only explicitly logical mutable state. It performs no callback, clock read, source/native operation, join, snapshot allocation or const cast. This is a single-control-owner in-memory API, not a claim of concurrent SessionIntent access, arbitrary allocator interception, physical sensor accuracy or hard scheduling/native stop latency.

## Reproduction and verification

All ten scenarios first fail in both ordinary and ASan/UBSan builds: core/extended parent native fault, explicit intent stop, foreign update/current read, foreign first start and current revocation under allocation refusal. In each profile the stale first-start path fails seven of fourteen checks and stale mutation fails three of sixteen before the fix. The fixture executes no helper, game/device scan, sensor, network or worker.

`test_thermal_current_views` supplies synthetic raw temperatures, original field metadata and explicit fixture thresholds. The two allocation cases first prove that the separately linked C++ `new` replacement refuses a control allocation, reset the counter, then require zero new attempts during current revocation. This does not replace the actual thermal/source tests or infer complete safety from a partial limiter. Unknown modes refuse before construction.

Fresh cross-platform/default-off, original safety/thermal/native consumer, repeated-case, privacy and cleanup results are recorded in the execution ledger only after completion. Actual source/threshold/full critical-safety/fleet/game-experience, remaining latency/export/deployment/physical/release gates stay open.

The final Windows default-off full run passes **418/418 CTest**, and all **15** affected cases pass five further repetitions. Both Linux ordinary and own-code **ASan+UBSan with leak detection** builds pass all ten final current scenarios plus five further repetitions, using five freshly built production units. Native ABI/late-owner/raw-safety/thermal/lifecycle **239/25/169/347/30** pass; three thermal source hashes remain unchanged. Exact staged/source/history privacy and owned-child cleanup are recorded in the execution ledger. These are CPU/fixture results, not complete safety or native sensor qualification.
