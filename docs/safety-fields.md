# Extended raw safety fields and memory limits (P-03g / P-11m)

M-SUPERVISOR/M-GAME; R-SAFE-001/002/004 and R-GAME-004; T-INTENT/T-GAME/T-PRIVACY; D-74.

This extends the existing [isolated collector](safety-telemetry.md) and [thermal limiter](core-thermal-guard.md) with an explicitly selected `ExtendedFields` profile. The default remains `CoreEvents`, and collection remains disabled. **ExtendedWithinLimits is a bounded thermal-limiter result, not complete safety qualification, a `WorkerSafetyEvidence` adapter or mining authority.**

## Required observations and native ABI

The extended profile requires the existing core temperature/current-supported clock events plus six ordered `nvmlDeviceGetFieldValues` observations:

| NVML field ID | Meaning | Handling |
|---|---|---|
| 82 | Memory temperature | Exact integral Celsius, representability -273..250; separate explicit operating limits |
| 193 | Shutdown T.Limit | Preserve original integral type/value; no absolute-temperature conversion |
| 194 | Hardware slowdown T.Limit | Preserve original integral type/value |
| 195 | Memory maximum T.Limit | Preserve original integral type/value |
| 196 | GPU maximum T.Limit | Preserve original integral type/value |
| 230 | GPU recovery action | Only zero is a usable observation; every nonzero value, including future enum extensions, is DeviceFault |

The helper resolves the additional read-only symbol only for the extended profile. Missing support does not fall back to the core-only profile or the older threshold getter. The private ABI is checked for a 40-byte field structure, 8-byte value union, timestamp/latency/type/status/value offsets 8/16/24/28/32, and a 12-byte TemperatureV1. The native `unsigned long` member follows the platform ABI; normalization checks its range before conversion. No vendor header is copied into the product.

An overall NVML success means **any** field was populated. Every field's own return code, unchanged ID, zero device-wide scope, value type, timestamp and latency is independently checked. Outputs start with invalid timestamp/type/status sentinels and an all-bits-set value union, preventing metadata-only writes from manufacturing a zero memory temperature or recovery action. Current clock events likewise start all-bits-set, not clear. Integral types 1..6 are decoded through their correct union members, with checked signed-64 normalization; floating-point, unknown types and unrepresentable unsigned values reject. Zero is a real observation only after all those checks.

Every returned field is examined after an overall successful call, even if an earlier field is unavailable. A later device-lost/reset-required error or nonzero recovery action wins over unsupported fields. Invalid/clock/unavailable/unsupported failures have descending precedence below DeviceFault. A failing overall call does not authorize reading its undefined field outputs. Existing checked shutdown, UUID-before/after and whole-batch withdrawal remain in force. No recovery command is executed.

## Original time and bounded private protocol

`CMSTQ1`/`CMSTA1` remain byte-compatible and retain their 16-KiB ceiling. Selecting the extended profile uses `CMSTQ2`/`CMSTA2`, with a 32-KiB ceiling for up to 64 devices and all six fields. The profile is part of the request digest through its version tag; cross-profile replies and transplanted v1 bindings reject. There is no negotiation or automatic downgrade. The shared process transport's independent allocation limits are unchanged.

Each field preserves its original NVML CPU UTC timestamp in microseconds, update latency in microseconds, integral type, field ID and exact signed integer. The original batch's dual clocks still span loading through unloading. A successful field timestamp must lie within that collection's UTC milliseconds: at least `began.utcMs * 1000`, at most `finished.utcMs * 1000 + 999`. The upper endpoint reflects the collector's millisecond resolution, not permission for a later millisecond or a new timestamp. Latency must be nonnegative and no greater than the collection budget in microseconds; it is not subtracted from time or used to create a monotonic timestamp.

An old cached field makes the batch unavailable. Missing/zero timestamps and invalid latency reject; future timestamps fault. The client and limiter revalidate the entire format. Per-field timestamps never replace the original batch begin as the conservative evidence-age anchor. This strict current-collection requirement may exclude a driver that returns cached/static fields; such a driver is not silently accepted as qualified.

## Explicit memory hysteresis and capability drift

The frozen limiter policy chooses exactly one profile. Core-only policies reject nonzero memory limits instead of silently ignoring requested memory protection. Extended policies require every selected device to have both an explicit core pause/resume pair and an explicit memory pause/resume pair, with resume strictly below pause. No model-wide thermal limits are shipped.

Every source request must match that frozen profile. The first fresh complete observation freezes each field's integral type and the four raw T.Limit values, along with the existing supported event mask. A later type or T.Limit change faults the session; subsequent cooler readings cannot clear it. T.Limits are **not** compared to the absolute core/memory temperature, interpreted as user thresholds, or treated as evidence that the user's operating limits are physically qualified.

Any selected device reaching either pause threshold holds the whole set. Recovery requires every selected device to be at or below both resume thresholds, with the required event capabilities and no hold event, throughout the existing consecutive-fresh-sample cooling policy. Cached ticks, skipped observations and missing data cannot advance cooling. `ExtendedWithinLimits` has the same original-time, exclusive, non-extending deadline and terminal-stop semantics as `CoreWithinLimits`; the two result names stay distinct.

## Interface sources and limits

The official NVML `latest` reference reviewed here identifies version 615. Numeric field IDs, integral enum values and native layouts were also checked against the CUDA 12.9 public header.

- [Field queries](https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlFieldValueQueries.html): overall-success versus per-field return semantics.
- [NVML reference, nvmlFieldValue_t](https://docs.nvidia.com/deploy/nvml-api/latest/api/pdf/NVML_API_Reference_Guide.pdf): original CPU microsecond timestamp, update latency, type and per-field status.
- [Field identifiers](https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlFieldValueEnums.html): memory temperature, T.Limits and GPU recovery action.
- [Device queries](https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlDeviceQueries.html): temperature APIs, margin-temperature distinction and the older threshold getter's Ada-and-later migration note.
- [Recovery actions](https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlVgpuStructs.html): none versus reset/reboot/drain and extended actions. These are observed states, not permission to execute their suggested actions.

This profile does not prove continuous Xid/ECC/event coverage, hotspot/VRM/fan coverage, sensor accuracy or the suitability of operating thresholds. Capability availability and timing on actual hardware remain untested. Full device/driver fault-model qualification, native safety review, source-to-fleet conversion, hard kernel-stall lifecycle/loader trust and ordinary non-Off activation remain open. On-machine integrated GPU/game acceptance stays deferred.

## Verification

Windows builds `test_safety_telemetry`, `test_safety_telemetry_native`, `test_safety_telemetry_owner`, `test_core_thermal_guard`, `safety_telemetry_fixture` and `canary_safety_telemetry_worker`, then runs:

```text
ctest --test-dir build/credits-check/windows -R "^(core_thermal_guard|safety_telemetry|safety_telemetry_native|safety_telemetry_owner|safety_telemetry_lifecycle|native_build_recipe|architecture_contract)$" --output-on-failure
```

Linux directly rebuilds the telemetry/core-limiter implementation, fixture and tests with `-std=c++17 -Isrc -O1 -Wall -Wextra -Werror -pthread`, reusing only the unchanged transport object. The sanitizer build adds `-g -fsanitize=address,undefined -fno-omit-frame-pointer`, uses the instrumented transport and enables leak detection without suppressions.

Windows focused CTest passes **7/7**. Windows/Linux ordinary and Linux ASan+UBSan/LSan each pass **169** codec/client/owner, **239** native ABI/error, **25** deterministic late-completion, **347** thermal-limiter and **30** parent-death/artifact checks, with zero failures. Both source profiles reach their distinct limiter result through the real owner and an owned CPU helper; source stop revokes them. Separate compile-only probes against the installed public header pass 23 layout/enum/ID assertions on both platforms without loading a driver. The initial Linux compile caught three test-only misleading-indentation warnings; the tests were split into explicit lines without suppressing diagnostics. Initial native tests passed 236 checks; final review added metadata-only value and unwritten-event sentinel checks before complete reruns.

Tests include the combined 64-device/6-field maximum frame, canonical signed extrema, cross-version substitution, individual native errors and unwritten outputs, all integral union types, stale/future originals, fault precedence, second-device withdrawal, capability drift, whole-set core/memory cooling and non-extending cached deadlines. Enabled sampling uses synthetic symbols or CPU helpers only; the real native helper receives disabled/malformed requests. Ordinary artifacts are checked for fixture seams and tuning/reset/remove/discovery APIs. These checks do not substitute for CUDA/SASS/protocol, real device, pool, gameplay or release qualification.
