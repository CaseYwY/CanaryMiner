# Strict user-pool TLS runtime

P-12h; M-TLS; R-TLS-001/003/004; T-TLS/T-CONFIG/T-INTENT.

This module connects the ordinary first-user-pool path to the retained TLS helper. It is available in a local build configured with `-DCANARY_ENABLE_POOL_TLS_RUNTIME=ON`. The option defaults off. Windows uses Schannel and system trust; Linux requires OpenSSL 3.5 or newer development libraries. Configuration downloads nothing. The miner and its fixed `canary_pool_tls_stream_worker` sibling must come from the same build, alongside the existing preference helper.

## Entry and scope

Use an explicit `--pool stratum+ssl://DNS:PORT` or the same URL in config-v1. The port must come from the chosen pool's documentation. No secure preset or port is guessed. The existing network and target-semantics requirements still apply to a custom pool. Disable donation explicitly with `--no-donation` or `"donation": false`.

Alternatively, save **strict TLS** for the first slot in Settings → Pools and TLS. Its DNS host must match the configured first pool; the saved declaration supplies the TLS port. If both command/config and saved settings specify strict TLS, their endpoints must agree. A saved declaration cannot redirect credentials to another host. TLS URLs use ASCII DNS names, including explicitly supplied punycode. IP literals, userinfo, paths and malformed names are refused.

The parent freezes the selected TLS URL and disabled-donation choice for worker launches and retries. Later preference edits affect a new explicit parent session. A restarted child cannot drop TLS or change the frozen host. This module does not freeze every unrelated field of the existing main configuration file.

Only ordinary first-user-pool mining is supported. Research/evidence modes, secure requests for unused pool slots, prefer-TLS/plaintext fallback and donation TLS are refused before device setup. Legacy TCP remains an explicit compatibility path with its warning. The module neither changes the donation schedule nor applies user TLS permission to the developer's peer. A defensive fee-session check also refuses an unexpected TLS donation attempt.

## Connection and failure behavior

The actual `runMiner` source construction owns a `PoolTlsRuntimeTransport` for the complete `PearlPoolSource` lifetime. Each open creates a fresh one-attempt process transport. The old helper is closed and settled first; the shared native admission epoch prevents replacement after unresolved ownership. Normal close permits a new TLS connection. Stop or loss of the original epoch permanently revokes this runtime owner. A caller from another scope cannot close or cancel the original owner's connection.

The helper performs DNS, connect and TLS processing. Authorization and proof bytes use the same verified connection. The ordinary runtime accepts system trust only; it has no private-CA, verification-disable, environment override or plaintext fallback path. The existing TLS engines retain TLS 1.2 minimum, reference-name verification and SNI. Schannel retains automatic chain validation, revocation checks and refusal of automatic client credentials. See [Microsoft's credential flags](https://learn.microsoft.com/en-us/windows/win32/api/schannel/ns-schannel-sch_credentials). Linux revocation qualification remains a separate release gate.

Connection setup has one ten-second budget. A stop signal is observed inside the process transport's open and record loops; it does not wait for the helper's native operation to finish naturally. Native OS termination/reaping limits remain those of the existing process owner, not a physical hard-deadline claim.

A reconnect performs fresh authorization. It does not replay an ambiguous submit. Attempted writes whose local reply was lost retain the canonical source's PossiblySent/Unknown evidence; they are not counted as accepted shares. Missing helpers, certificate/name errors, protocol errors and timeouts never select TCP.

## Validation and remaining acceptance

Final software validation on 2026-11-12:

| Configuration | Evidence and result |
| --- | --- |
| Windows, runtime off | Clean host build: 847 build steps succeeded. Full CTest run: 474/475 passed; the remaining test expected the old refusal wording. Its assertion was updated while retaining exit-code, no-device, privacy and file-preservation checks. `--rerun-failed` then passed 1/1. The full run includes the secret scan. |
| Windows, runtime on, private trust off | Clean focused build: 68 build steps succeeded, including the actual `run.cpp` artifact. Eight focused CTest groups passed: runtime owner, policy, actual inert CLI, existing process transport, check contract, Schannel stream, normal-trust rejection loopback and artifact isolation. A subsequent compatibility run passed release preferences, pool-security CLI and architecture contracts, 3/3. |
| Linux, runtime on, OpenSSL 3.5.5 | Seven focused CTest groups passed, including actual `runMiner` admission. The native stream and ordinary runtime rejection loopbacks passed 2/2; artifact isolation passed 1/1. The runtime-owner regression passed again after the final stop-latching correction. Existing config, CLI, privacy, pool-security and fee-session regressions passed 5/5. |

The policy suite covers 69 checks and the enabled actual inert CLI covers 43. The runtime owner covers 65 checks using real child-process IPC and synthetic protocol replies: repeat reconnect/reauthorization, partial transfers, failure before authorization, ambiguous submit without replay, missing helper, stop during open, permanent stop/epoch revocation, foreign-caller denial and rejected private trust. The independent normal-trust loopback confirms that an untrusted certificate receives no application authorization bytes. Encrypted positive fixture tests use a private test CA in the development helper; they do not qualify ordinary system-trust success.

Review corrected nested-scheme parsing, late research-environment admission, foreign callers affecting an existing owner, and a read-only stop observation that could otherwise be cleared before reconnect. Regression coverage accompanies those fixes. The broad Windows run also exposed a local MSVC/Ninja include-prefix encoding mismatch: header dependencies were missing and an old object requested the previous constructor signature. A build-local UTF-8 prefix correction was verified with `ninja -t deps`, then both Windows configurations were cleaned and rebuilt. The final runtime test object records the runtime, process and admission headers; earlier incremental Windows results are not used as final evidence.

Host-only reproduction starts with the normal tests profile. Set `CANARY_ENABLE_POOL_TLS_RUNTIME=ON` for the enabled directory; Linux additionally needs the OpenSSL development installation discoverable by CMake. The explicit localhost fixture options and scratch-directory contract are the same as [the retained TLS helper](pool-tls-stream.md). Do not run the trusted-private-CA Schannel fixture with private trust disabled.

```sh
cmake -S . -B build/tests-local -G Ninja -DCANARY_PROFILE=tests -DCANARY_ENABLE_POOL_TLS_RUNTIME=OFF
cmake --build build/tests-local
ctest --test-dir build/tests-local --output-on-failure
cmake -S . -B build/tests-tls -G Ninja -DCANARY_PROFILE=tests -DCANARY_ENABLE_POOL_TLS_RUNTIME=ON
cmake --build build/tests-tls
ctest --test-dir build/tests-tls --output-on-failure -R '^(pool_tls_runtime_transport|pool_tls_runtime_policy|pool_tls_runtime_cli|pool_tls_process_transport|pool_tls_check_contract|pearl_tls_stream|pool_tls_stream_artifacts)$'
```

Retained local logs are `build/windows-final-clean-build.log`, `build/windows-final-clean-ctest.log`, `build/windows-final-rerun-failed.log`, `build/windows-tls-final-clean-build.log`, `build/windows-tls-final-ctest.log`, `build/windows-tls-final-header-deps.log`, `build/windows-tls-final-compat-rerun.log`, `build/linux-tls-focused-ctest.log`, `build/linux-tls-loopback.log`, `build/linux-tls-runtime-final.log` and `build/linux-tls-artifacts.log`. They remain outside the public tree. The final Windows follow-up commands, after selecting the local CTest executable, were:

```sh
ctest --test-dir build/tests-local-windows --rerun-failed --output-on-failure
ctest --test-dir build/tests-tls-windows --output-on-failure -R '^(pool_security_cli|architecture_contract|release_preferences|aux_imports)$'
```

The second expression selected three tests; this build has no test named `aux_imports`. No zero-test invocation is counted as evidence.

No public pool authentication, real wallet, GPU mining, installed-program replacement or release publication is part of this module's software checks. The pinned CUDA 13.1 toolchain was unavailable for this run, so `tools/test_all.sh` and its production CUDA/SASS/pinned-verifier gates were not run. Public normal-trust positive acceptance, real pool/proof acceptance, secure donation tuples, consent-bound fallback and release/deployment qualification also remain open. The standard release builders explicitly keep this runtime off and reject an enabled TLS artifact until that separate qualification. P-12 remains partial.
