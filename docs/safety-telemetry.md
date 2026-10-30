# Isolated raw GPU safety observations (P-03d / P-11i)

M-SUPERVISOR/M-GAME; R-SAFE-001/002/004, R-GAME-004 and R-PRIV-002; T-INTENT/T-GAME/T-PRIVACY; D-70.

This slice provides a default-off background owner and a parent-bound read-only NVML helper for **GPU-core temperature and current/supported clock-event masks**. These raw observations do not constitute complete critical-safety evidence, `GameSafety`, a worker Run lease or permission to mine. Ordinary startup still refuses non-Off game requests before discovery under [P-11h](game-settings.md).

## Private request and batch contract

`SafetyTelemetryRequest` freezes an enabled flag, nonzero epoch/revision, original UTC/monotonic issue time, collection budget and ordered selected device set. It accepts 1..64 unique canonical lowercase full `GPU-` UUIDs, excluding the all-zero identity. It does not enumerate devices or fall back to an index. Device identities remain inside inherited local IPC and memory; no public stats, logging, file persistence or network output is added.

`CMSTQ1` and `CMSTA1` each have a 16-KiB protocol ceiling, canonical unsigned decimal tokens, exact line counts and no trailing input. The response binds every logical request field by SHA-256, including device order, original time and budget. The transport PID is checked separately by helper parent adoption; it is normalized in the logical digest, which is a binding checksum rather than authentication against a malicious same-user process. The shared transport retains its independent 256-KiB input/64-KiB output allocation ceilings before the stricter decoder rejects oversized telemetry frames.

A successful batch contains exactly one reading per requested device, in original order. **Any missing/failed/mismatched read withdraws the entire batch.** Zero degrees is a valid observation, not a missing-value marker. The broad representability range -273..250 C is only a wire/sanity bound, never an operating-temperature limit. Unknown supported event bits are preserved; an active bit outside the returned supported mask is rejected. A zero capability mask is observable but does not prove sufficient protection.

Collection time begins before loading/initializing NVML and finishes after shutdown and library unload. Success requires ordered issue/begin/end clocks, a 25..1000-ms collection budget on both clocks and at most 100 ms wall/monotonic divergence within collection. Failed replies carry no readings and retain actual timestamps, including rollback, instead of replacing elapsed failure time with zero. The client rejects stale/future requests, future or pre-transaction successful observations, late delivery and malformed bindings. Publication never refreshes observation time.

## Native adapter and capability scope

The helper validates its private request and adopts the owning parent before calling the adapter. The adapter dynamically resolves only:

- `nvmlInit_v2` and `nvmlShutdown`;
- `nvmlDeviceGetHandleByUUID` and `nvmlDeviceGetUUID`;
- `nvmlDeviceGetTemperatureV`, using the 12-byte TemperatureV1 ABI, version `12 | (1 << 24)` and GPU-die sensor 0;
- `nvmlDeviceGetCurrentClocksEventReasons` and `nvmlDeviceGetSupportedClocksEventReasons`.

UUID is checked before and after each device's readings. Every read's return code is checked independently. Temperature starts with an out-of-range sentinel, so an unwritten output cannot appear as zero. Missing modern symbols yield Unsupported; there is no deprecated-temperature or throttle-reason fallback. NVML errors 15 (GPU lost) and 16 (reset required) are DeviceFault observations, never reset instructions. Shutdown is checked even after an earlier failure, and a later device fault cannot be masked by an earlier unsupported/unavailable read. Existing stronger failures survive cleanup errors. Exceptions or a non-exiting helper publish no usable batch.

Windows currently loads only an absolute `GetSystemDirectoryW()` DCH `nvml.dll`, with `LOAD_LIBRARY_SEARCH_SYSTEM32`. A standard-driver NVSMI installation is outside this initial capability; PATH/CWD probing is not used. Linux uses `dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL)` under the shared helper's minimal environment. Distribution loader configuration, library/dependency provenance and installation trust still require deployment qualification. The source adapter is intended only for the helper; its synchronous driver calls must not run on the supervisor/control thread.

### Primary interface evidence

The official NVML reference reviewed for this slice identifies itself as version 615; its unversioned entry redirects to `latest`. The ABI was also checked against the installed CUDA 12.9 public NVML declarations without copying the vendor header into this project.

- [Device queries](https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlDeviceQueries.html): UUID lookup, TemperatureV, current/supported event queries and return semantics. The older temperature getter is deprecated in favor of TemperatureV.
- [Clock event reasons](https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlClocksEventReasons.html): multiple simultaneous bits, thermal/power/other reasons and extension of supported masks.
- [NVML API reference and library locations](https://docs.nvidia.com/deploy/nvml-api/latest/nvml-api-reference.html): Windows DCH/System32 versus standard-driver NVSMI installation and Linux library lookup.

These sources establish API meaning, not accuracy, complete safety coverage or physical qualification on a particular device/driver. Memory/hotspot temperatures, full fault/event monitoring and capability requirements remain separate work.

## Fixed background ownership

`SafetyTelemetryOwner` copies its device set and policy, starts at most once and permits one helper flight at a time. Defaults are disabled, 500-ms poll, 1000-ms helper timeout and 500-ms collection budget. Enabled policies require poll 100..10000 ms and timeout 50..2000 ms, with timeout at least the collection budget. Each completion schedules the next poll from completion time; there is no catch-up burst or immediate retry loop.

Snapshots are immutable. Ready means a successfully decoded raw batch, not current freshness or thermal safety. A previous batch may remain visible during a pending flight with its original request/time; consumers must apply their own qualified validity policy. Unsupported/unavailable results move to Waiting without usable readings. Identity/device/invalid faults and clock reversal/drift latch the owner; there is no reset, retarget or restart API. Parent clock consistency has an independent 2000-ms lifetime drift limit.

`requestStop()` revokes publication/admission and sets cancellation before any join. A late successful result cannot revive the owner. `stop()` joins outside the snapshot mutex. Even after cancellation, a late Unreaped result retains the exact request, transport status and unresolved-ownership flag; repeated stop cannot erase it. Real helper calls and native teardown remain on the background thread. The shared process wrapper uses Windows owned jobs or Linux adopted-parent death signals and a bounded reap attempt; genuine kernel stalls, global cross-helper quarantine, loader/entry-before-adoption death and hard stop deadlines remain unqualified.

## Build and verification

CMake registers separate core, native adapter and worker targets in both configuration branches. No ordinary supervisor factory, CLI switch or installation rule activates them. The focused Windows invocation, after entering the native MSVC/CMake environment, was:

```text
cmake --build build/credits-check/windows --parallel 2 --target test_safety_telemetry test_safety_telemetry_native test_safety_telemetry_owner safety_telemetry_fixture canary_safety_telemetry_worker
ctest --test-dir build/credits-check/windows -R "^(safety_telemetry|safety_telemetry_native|safety_telemetry_owner|safety_telemetry_lifecycle|native_build_recipe|architecture_contract)$" --output-on-failure
```

Linux validation uses direct GCC (`-std=c++17 -Isrc -O1 -Wall -Wextra -Werror -pthread`) rather than Linux CMake. It compiles the three new implementation units and native worker/CPU fixtures/tests against the existing unchanged `local_task_process` object, linking the production adapter with `-ldl`. Native-adapter and late-owner tests separately compile the actual adapter/owner with their respective test-only macro. The sanitizer build adds `-g -fsanitize=address,undefined -fno-omit-frame-pointer`, uses the previously instrumented unchanged transport and enables `detect_leaks=1`, with no suppressions.

Final Windows focused CTest passes **6/6**. Windows/Linux each pass **118** codec/client/owner checks, **111** native ABI/error/identity checks, **25** deterministic late-completion checks and **21** owned-parent-death/artifact checks. The same Linux selection passes ASan+UBSan with leak detection. Initial versions passed Windows 5/5 and Linux 106/96/13; source review then added checked cleanup on failure, original failed-clock retention, unwritten-temperature rejection and explicit late-Unreaped diagnostics before the final runs.

The native-adapter test replaces only symbol lookup and aborts on any unexpected API. It checks multi-device full-batch withdrawal, every used API's failure classes, UUID substitution, actual TemperatureV1 inputs, unknown masks and slow initialization/symbol resolution/read/shutdown. The owner seam separately controls late completion after stop; it does not simulate a genuine unreapable OS process. Production core/native/worker artifact scans exclude both seams, fixture markers, setters, index discovery and CUDA discovery strings. The real production helper receives only disabled/malformed requests in tests; all enabled sampling uses synthetic symbols or an owned CPU helper. No actual GPU or game is sampled by this validation.

[P-03e/P-11j](core-thermal-guard.md) adds a separate pure limiter with explicit per-device core thresholds, required event capabilities and fresh consecutive cooling. Full critical-field coverage, actual device/threshold-source qualification, a complete device/driver fault model, qualified evidence conversion and fleet/CLI admission remain integration work. Existing display telemetry and synchronous settings/storage/display calls retain their independent limitations. Full CUDA/SASS/verifier/protocol, live pool, physical GPU/game and release gates remain open; on-machine integrated acceptance stays deferred until implementation is ready.
