# P-12i: local TLS installation layout

An enabled developer build now installs `canary_pool_tls_stream_worker` in the same `bin` directory as the miner and `canary_preferences_worker`. The TLS runtime resolves its helper beside the actual executable. Copying only the miner is incomplete. Runtime-off installs do not add the TLS helper.

This is a local CMake installation rule. It does not qualify a release archive, enable TLS in the release builders, or install a certificate. The existing release verifier still rejects a cache with `CANARY_ENABLE_POOL_TLS_RUNTIME=ON`.

## Host-only installation check

The tests profile provides a `pool-tls-runtime-host` component when TLS runtime is enabled. It contains exactly the inert `test_pearl_cuda_cli_host` executable, the genuine preference helper and the genuine TLS helper. The inert executable stops at the device-setup sentinel; it is not a miner for deployment.

This test component is excluded from an unqualified `cmake --install`. Its generated install rules require the explicit component name. Existing unrelated host components retain their previous installation behavior; this change does not claim that a default tests-profile install produces an empty directory.

```sh
cmake -S . -B build/tests-tls -G Ninja -DCANARY_PROFILE=tests -DCANARY_ENABLE_POOL_TLS_RUNTIME=ON
cmake --build build/tests-tls --target test_pearl_cuda_cli_host
ctest --test-dir build/tests-tls --output-on-failure -R '^pool_tls_runtime_install$'
```

Linux additionally needs OpenSSL 3.5 or newer development libraries. Actual TLS runtime requires a glibc 2.40-or-newer build/runtime and kernel pidfd support for its isolated process owner; a host-only installation inventory does not prove that capability. The test executes `cmake --install` into an owned temporary path containing spaces and Unicode. It checks the exact three-file inventory, byte identity with the selected build, regular files and executable permissions. It also checks the TLS helper for known GPU-loader markers; this is not a full platform dependency qualification.

The installed inert CLI reaches its device sentinel from an unrelated working directory. Removing the installed preference helper causes startup refusal even when a genuine helper exists in CWD and PATH; restoring it restores admission. Configuration bytes remain unchanged. No device, pool connection or trust-store API is exercised by this test.

The inert CLI stops before creating a pool transport, so this installation check does not independently exercise a missing TLS helper. The existing `pool_tls_runtime_transport` regression covers missing-helper refusal and no plaintext fallback. Actual production CUDA installation, normal-trust positive pool acceptance and release/deployment qualification remain separate gates.

The release-boundary check first accepts a minimal cache with all required capabilities off, then changes only the TLS runtime flag to on and requires rejection. It also requires the actual TLS-enabled host cache to remain ineligible for release.

## Validation

On 2026-11-13, the final install and release-preferences CTests passed 2/2 on Windows and 2/2 on WSL Linux. The installation test passed 38 checks on each platform. The existing Windows runtime-transport regression, including missing TLS helper refusal, also passed. These were CPU-only checks against local artifacts.

The local logs are `build/p12i-windows-install-final.log`, `build/p12i-linux-install-final.log` and `build/p12i-windows-ctest.log`. No production CUDA build, installed miner replacement or release archive was created.

## Subsequent production installation check

[P-12j](pool-tls-fallback.md#production-artifact-checks) also built the actual Linux TLS-enabled CUDA program and installed its three-file layout into an isolated Unicode/space path. All installed hashes matched their build artifacts. Real help/version and pre-device refusal checks passed; removing the preferences sibling refused startup despite CWD/PATH alternatives. The actual production TLS helper separately passed localhost runtime/protocol tests through CPU probes. These extend the host-only layout evidence above; no real GPU session, main-program missing-TLS-helper run, external pool authorization or release publication was performed.
