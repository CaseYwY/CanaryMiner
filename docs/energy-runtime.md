# Native development energy runtime (P-08i)

This slice wires the existing [authorization controls](energy-authorization.md) and [lifetime-fenced collector](energy-collector.md) into the native entry point behind **`CANARY_ENABLE_ENERGY_RUNTIME=OFF` by default**. R-ENG-001, R-PRIV-001/002, R-SAFE-001/002 and R-REL-001 remain partial; M-ENERGY/M-PRIVACY/M-SUPERVISOR; T-ENERGY/T-CONFIG/T-INTENT/T-UI/T-PRIVACY/T-RELEASE; D-43.

Build capability, saved consent and current collection are separate. Enabling the development build does not enroll devices or enable disk retention. A missing authorization record remains OFF and read-only startup creates no state directory, lock, identity or database. A saved ON requires the existing Ready enrollment, exact selected set and successful fresh lifetime handshake. This is host integration evidence, not a qualified production deployment or real-sensor measurement.

## One explicit parent, one startup decision

`EnergyRuntime` implements the existing capture sink and adds bounded authorization callbacks. The normal supervised `run` entry point creates it once, after explicit session intent and selected GPU identities are known. Child processes and child retries do not construct another runtime. CLI `--no-energy` suppresses capture construction but leaves next-parent authorization settings available.

For an eligible mining parent, startup first invokes the single-start leased capture. Its asynchronous Load decides OFF/unavailable or attempts a fresh fenced Provision. **The settings owner is not started until that decision resolves.** A STARTING snapshot admits no settings action. Once the handshake succeeds or startup terminates, a new authorization Load sees the post-startup CAS; the renderer never exposes an editable stale pre-Provision baseline.

The supervisor's memory-only tick advances this barrier. Starting the settings owner does not wait for a disk/helper on the supervisor thread. Its first read and every save/enrollment/inspection still run through the isolated owner. The UI callback explicitly rejects Provision and rejects enrollment with a substituted device selection. All collection authority comes from the lifetime handshake, never the settings owner or a previously saved session ID.

Saved consent/cadence is next-explicit-parent only. Saving OFF does not rewrite the current frozen plan; saving ON in an OFF parent does not begin collection. Q/Ctrl+C stops the current parent. An enabled future explicit parent obtains a new session/epoch. Lost/missing/wrong helpers, changed selection and corrupt records remain unavailable; there is no automatic restart, repair or adoption.

## Idle settings and native feedback

`settings` still runs before CUDA initialization. It checks TTY and prior stop before creating the runtime. This mode starts only the read/edit authorization owner and never provisions or collects, even if saved consent is ON.

Offline enrollment requires the configured full explicit UUID set. Empty, `all`, ordinal, short, duplicate or mixed-invalid selections make enrollment unavailable; settings does not discover GPUs or silently resolve indices. The running supervisor instead supplies its selected complete device identities. The page hides them by default and shows a clear selection-unavailable explanation.

The existing Electricity/Privacy page retains draft/review/save/cancel behavior. Current capture status is read dynamically: starting, frozen active plan, recovery review, unavailable or not active. Saved ON/OFF and CLI override remain distinct. Missing/wrong authorization helpers show unavailable, with no fake confirmation. These additions use existing ASCII wrapping/scrolling and keyboard controls, without new GUI/browser dependencies.

## Revocation and lifetime

`requestStop` revokes capture and authorization admission using memory-only calls and freezes both published snapshots. Every late submit/inspect/retry/acknowledge is refused, including after joining. A pre-start stop consumes the instance; it cannot be started afterward. Unknown original reservations/settings inputs are retained under their existing contracts.

Supervisor stop revokes before worker drain, and joins optional helpers after miners are drained. Settings close revokes before joining. Signal checks precede each queued terminal key; a received stop cannot be followed by another queued settings save. No callback revives the runtime and no saved switch restarts mining. Already admitted writes may complete; stop is not rollback.

## Fixed sibling layout and trust boundary

The native factory resolves its own module through Windows `GetModuleFileNameW` or Linux `/proc/self/exe`, with explicit bounds/truncation refusal. It constructs only these siblings in that executable's directory:

- `canary_energy_authorization_worker` (`.exe` on Windows)
- `canary_energy_collector_worker` (`.exe` on Windows)

There is no PATH/CWD executable search, executable config field, environment override, download or fallback. Unicode paths use native Windows wide APIs and UTF-8 configuration paths. The configured state directory becomes absolute while preserving its traversal semantics; `energy-persistence.json` is appended. Parent-side discovery does not probe/read helper files. A helper missing or speaking the wrong protocol fails through the existing bounded transport.

Both the executable directory and private state directory must be protected by deployment. Fixed names/absolute paths are not signatures or defense against same-privilege replacement, symlink/reparse races, copied authorization records or hostile library loading. Dependencies and loader behavior on a clean machine remain qualification work. SQLite stays the existing optional system ABI; this change does not bundle/download it or infer authorization from its presence.

An enabled CMake native target depends on both helper targets and places them beside the main executable for single- and multi-config generators. `cmake --install` installs all three into `bin`. The CPU-only `energy-runtime-host` install component instead installs the explicitly named capability-enabled test CLI and both ordinary helpers. This component is for private host verification, never a replacement miner.

The existing production release scripts explicitly configure the development capability OFF and require its OFF cache record before packaging their single-binary layout. They cannot silently package an enabled binary while omitting helpers. No production archive, upstream submission or installed-miner replacement is performed by this batch.

## Verification and remaining gates

`test_energy_runtime` covers startup mutation exclusion with an outstanding handshake, fresh post-Provision authorization state, frozen OFF/ON behavior across parents, reorder/selection rejection, idle/CLI suppression, callback revocation, stop-before-start, helper/protocol faults, corrupt-store preservation and private-default rendering. Layout probes execute copied binaries in a Unicode/spaces installation, prove genuine CWD/PATH helpers cannot replace an absent sibling, reject non-TTY settings before I/O and check fixture-hook exclusion. The Windows install probe verifies the actual CMake component.

Native PTY coverage uses the actual capability-enabled host CLI for offline enrollment/Save/reopen/CLI/selection behavior and the synthetic supervisor for active-OFF, inactive-ON and fresh-parent transitions. The latter launches synthetic workers with UUID-shaped fixtures; it cannot establish physical NVML accuracy, energy coverage or mining economics. Ordinary/default-off CLI regressions continue separately.

Executed evidence (2026-10-29):

- Windows/MSVC: **60/60 distinct focused CTest cases** (the new runtime/layout pair, existing 56-case regression and collector/lifecycle pair). Runtime **109 checks** and **five repetitions** pass; sibling/layout/artifact/install probe **26 checks**. The actual `energy-runtime-host` component contains exactly the enabled host CLI and its two required helpers.
- Linux/WSL/GCC: runtime **109 checks**, **five repetitions**, and own runtime/core/helper **ASan+UBSan 109** pass. Unicode sibling/no-fallback/artifact layout **22 checks**; Linux does not run the four Windows CMake-component assertions. Builds use direct GCC with warnings as errors, not Linux CMake or production CUDA.
- Native capability-enabled settings and synthetic-supervisor PTY **42 checks**, including current-OFF/save-ON isolation, fresh-parent session issuance, idle/CLI/ordinal selection, default-hidden identities, public stats exclusion and controlling-terminal SIGINT. Synthetic supervisor **246 checks** passes.
- Linux regressions: authorization **1481/124** (owner and independent reconstruction), collector/lifecycle **366/62**, capture **1392**, recovery **359**, compaction **3674**; leased/recovery/authorization PTY **20/19/21**, session-energy/tariff/manual-market PTY **24/16/20**, settings/runtime-settings PTY pass. Python discovery **123 tests with seven explicit fixture-dependent skips**. Windows also covers the existing credit/cursor, exact arithmetic/oracle, donation, intent, native settings and build/import contracts.
- Windows release script parses successfully and Linux release script passes `bash -n`; their production build/package paths were not executed. Final architecture contracts and staged privacy/artifact review are separate checks.

Final source review places both owner revocations before optional stop-snapshot allocation. Allocation failure itself is not fault-injected. The focused runs report no functional test failures; these results do not establish actual kernel/disk failure deadlines or production installed-miner behavior.

The test profile can reproduce this batch with:

```text
cmake --preset tests
cmake --build --preset tests --target test_energy_runtime test_pearl_energy_runtime_cli_host test_pearl_cuda_cli_host test_energy_runtime_supervisor energy_collector_worker_fixture energy_authorization_fixture
ctest --preset tests -R "^energy_runtime" --output-on-failure
```

Unfinished gates include global helper quarantine/reaping and Linux parent-death binding for other helper kinds; real loader/spawn/kernel/disk/power-failure qualification; protected installation/dependency provenance; selected-set change/orphan enrollment workflow; real-cadence capacity/archival policy; production CUDA/physical telemetry; persisted monetary billing continuation; actual-income sources and today's forecast/net view. P-08 and the full roadmap are still incomplete.
