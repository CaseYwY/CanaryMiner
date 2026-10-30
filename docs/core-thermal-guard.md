# Explicit core-temperature limits and cooling (P-03e / P-11j)

M-SUPERVISOR/M-GAME; R-SAFE-001/002, R-GAME-004 and R-PRIV-002; T-INTENT/T-GAME; D-71.

`CoreThermalGuard` is a pure, default-off, selected-device limiter over [isolated raw telemetry](safety-telemetry.md). It requires an existing explicit `SessionIntent` token and starts at most once. It neither starts a collector/worker nor changes GPU settings. **CoreWithinLimits is only this limiter's result, not complete `GameSafety`, physical qualification or mining authority.**

## Explicit frozen policy

Every selected device needs its exact full UUID and two explicitly supplied integral-Celsius thresholds: `pauseAtC` and the strictly lower `resumeAtOrBelowC`. Equal default values are invalid; there is no shipped model-wide operating temperature. The -273..250 C representability bounds are inherited from the raw format, not recommended operating limits. Actual device-specific threshold sourcing/review and complete critical-safety qualification remain prerequisites for production admission.

The constructor copies the ordered device set and all controls; there is no live edit or retarget method. Enabled policies require:

- 1..64 unique canonical nonzero full UUIDs, matching every source request in exactly the same order;
- maximum age 100..5000 ms;
- maximum observation gap 100 ms through the chosen maximum age;
- cooling duration 100..600000 ms, independently configured from sampling/age;
- 2..10000 distinct consecutive cool samples.

Defaults retain 2000-ms age/gap, 30000-ms cooling and two samples, but remain disabled without explicit thresholds. These are software policy controls, not measured physical guarantees.

## Event capabilities and whole-set hold

The initial event dialect recognizes the NVML bits `0x1ff`: GPU idle, application clocks, software power cap, hardware slowdown, sync boost, software thermal slowdown, hardware thermal slowdown, hardware power brake and display clocks. Numeric declarations were checked against the public NVML header used for [D-70's interface evidence](safety-telemetry.md#primary-interface-evidence).

Each device must report support for every hold bit `0xe8` (hardware slowdown, software/hardware thermal slowdown and hardware power brake). Any active hold bit pauses the entire selected set. Hardware slowdown/power brake is conservatively treated as a hold, not proof of a particular thermal cause. Missing required support or an active unknown bit is Unsupported; it cannot supply cooling progress. Unknown inactive supported bits remain observable. The first fresh batch freezes each supported mask; later capability changes fault the session rather than silently accepting a new environment.

The whole set holds when **any** temperature reaches or exceeds its pause threshold. Once admitted below the limits, temperatures between resume and pause thresholds preserve hysteresis. After a hold, every device must be at or below its resume threshold with no active hold/unknown event before cooling can begin.

## Cooling and time evidence

Startup begins held. The first fully cool fresh batch starts the cooling interval at its **finish**; a later batch can complete it only at that batch's **begin**, after both UTC and monotonic elapsed times reach the configured duration and the distinct-sample minimum is met. Thus collection duration is not retroactively treated as proven all-device cooling. This is a bounded sampled policy, not continuous physical-temperature proof.

Repeated snapshots do not increment samples or advance the last physical observation. A skipped revision, excessive gap on either clock, unavailable/unsupported batch, transport timeout, stale batch or a warm reading while cooling resets the sequence. Fresh complete cooling must then start again. Missing readings never become zero. Source cancellation/stop, device/identity/invalid faults, malformed bindings, changed same-revision receipts and source rollback latch; a new start on the same guard is refused.

Every request must match the explicit epoch, frozen UUID set and original collection budget. Canonical request/reply encoding checks the full batch. Completed count and request revision must agree. New requests advance issue time and cannot start before a previously observed failed or successful batch finished. All original clocks remain aligned to the guard's start with the independent 2000-ms lifetime drift ceiling; backward/future/overlapping times and deadline overflow fault.

An admitted view keeps the raw batch's beginning as `observed`. Its monotonic deadline is clipped by the worse of original UTC/monotonic ages and is exclusive: equality is expired. Re-evaluating the **same** receipt can only shorten that deadline. Even a later reduction in wall/monotonic skew cannot extend previously published cached validity. Callers must honor the deadline; reading `snapshot()` does not make it fresh.

Explicit stop revokes immediately and survives a later session token. A late unresolved-source flag/Unreaped transport remains visible after stop, without restoring eligibility. The guard performs bounded memory work only; it makes no filesystem, network or driver call.

## Verification scope

Synthetic CPU cases cover threshold equality and hysteresis, initial cooling, minimum distinct samples, exact collection/end/start timing, whole-device aggregation, event capability/unknown-mask handling, skipped/duplicate/substituted/old revisions, unavailable/timeout resets, original deadlines, skew reduction, malformed sources, clock overlap/overflow and terminal stop. The threshold numbers in tests are synthetic examples, not product defaults or device guidance.

An integration case uses the real default-off telemetry owner and its owned synthetic helper. It reaches CoreWithinLimits from actual immutable request/reply snapshots, then verifies that source stop revokes the limiter and neither component restarts. It performs no native GPU sampling.

Windows builds `test_core_thermal_guard` together with the four safety telemetry programs and runs CTest selection `core_thermal_guard|safety_telemetry|safety_telemetry_native|safety_telemetry_owner|safety_telemetry_lifecycle|architecture_contract`. Linux directly compiles the new guard/test with `-std=c++17 -Isrc -O1 -Wall -Wextra -Werror -pthread` and the unchanged safety-telemetry/owner/transport objects. The instrumented variant uses `-g -fsanitize=address,undefined -fno-omit-frame-pointer`, instrumented dependencies and leak detection without suppressions. Final evidence belongs in the [execution ledger](architecture/execution-plan.md).

Final Windows focused CTest passes **6/6**; Windows/Linux each pass **262** guard checks, including real owner/CPU-helper handoff. Linux ASan+UBSan with leak detection also passes **262** using unchanged instrumented dependencies. Re-linked Windows raw telemetry/client/owner **118**, native ABI **111**, late completion **25** and lifecycle/artifact **21** pass. The initial **253** checks passed before final review added failed-batch overlap, late transport-only Unreaped and real-owner integration cases. Architecture/import guards pass separately. These are host/synthetic results, not on-machine thermal or gameplay qualification.

Full critical-field/fault coverage, real device/driver and threshold-source qualification, native settings review, source-to-fleet permission conversion and ordinary CLI binding remain follow-ups. Ordinary non-Off game startup remains refused. The existing display telemetry path is separate; integrated on-machine GPU/game acceptance is deferred until implementation is ready.
