# P-12k: explicit strict TLS preset

M-TLS/M-CONFIG; R-TLS-001/003/004; T-TLS/T-CONFIG/T-INTENT.

The `kryptex-tls` preset selects strict TLS for the first user pool. It expands to `stratum+ssl://prl-us.kryptex.network:8048`, using the US endpoint and TLS port in the [official Kryptex instructions](https://pool.kryptex.com/articles/pearl-miners-en). The parsed pool identity remains `kryptex`, so its existing network, target and gzip-v2 negotiation rules still apply.

Use it in a [TLS-enabled local build](pool-tls-runtime.md), with donation explicitly off:

```sh
canary-miner --pool kryptex-tls --no-donation --wallet YOUR_PRL_ADDRESS.rig1
```

For a new JSON configuration, copy [config.tls.example.json](../config/config.tls.example.json), replace the wallet placeholder and pass the copy with `--config`. This separate example selects the strict preset and disables donation. It does not rewrite an existing configuration or change the default donation setting.

The alias always means strict TLS, including in a build without the native runtime. Such a build refuses startup before device setup instead of changing the endpoint to TCP. The installed miner and helpers must come from the same enabled build. Donation enabled, TLS in an unused pool slot and incompatible research modes retain their existing startup refusal.

Saved Prefer TLS or earlier plaintext consent cannot weaken this preset. Conflicting saved TLS endpoints are refused. Worker launches freeze the strict SSL endpoint and carry no plaintext fallback parameter; retries retain that decision.

The old `kryptex` preset still selects TCP port 7048. Existing explicit URLs and their ports keep their meaning. No pool-region selection, automatic configuration migration, first-run wizard, HeroMiners/F2Pool TLS preset or donation TLS route is added. The separate [consent-bound fallback](pool-tls-fallback.md) remains available only through its existing explicit declaration flow.

## Validation

On 2026-11-13, Linux TLS ON and OFF builds each passed the six focused CTest groups with zero skips. Each build passed 110 configuration checks, 112 CLI unit checks, 116 policy checks and 89 actual inert-CLI entry/preservation checks; schema validation passed twelve tests, and the CLI privacy regression passed. The ON build used GCC 15.2/glibc 2.42; OFF used the isolated GCC 14.2/glibc 2.40 gate environment. The five rebuilt targets were `test_product_config`, `test_product_cli`, `test_pool_tls_runtime_policy`, `test_pearl_cuda_cli_host` and `canary_preferences_worker`, with four build jobs.

The regressions cover the exact mapping and canonical protocol identity, JSON and command-line inputs, explicit compression/target overrides, unchanged TCP presets, enabled/disabled admission, donation/unused-slot refusals, saved Prefer TLS suppression and strict worker endpoint freezing. Tests do not open a pool connection or initialize a GPU. Independent review of all eleven implementation/test files found no actionable defect; a separate documentation review also found none. The reviewer independently ran the twelve schema tests. The architecture contract passed four tests.

Logs are retained as `build/p12k-linux-{on,off}-{build,ctest,test-details}.log`. An initial OFF test-launch command failed at shell parsing before running any tests; that failure is retained separately as `build/p12k-linux-off-ctest-launch-error.log`. The subsequent direct test invocation produced the six passing results. The first full-gate attempt at `961e7f1` was interrupted for the Windows build repairs below: 575 CTests had passed, one was skipped and one failed because the root-fusion CLI test still matched the old preset-list diagnostic. That assertion now includes the new preset and retains the private-original omission check. History scanning and SASS had not run in that attempt, so it is not a full-gate pass. Its evidence is separate under `build/p12k-gate-evidence/run1-interrupted/`. Final full-gate and Windows production-artifact qualification remain pending.

The earlier public endpoint checks established system-trust handshakes on Windows and Linux with zero application bytes. They did not authenticate a wallet, obtain a mining job or establish proof acceptance. P-12 and release qualification remain incomplete.

## Windows CUDA text repair

Actual Windows production compilation exposed an existing source-encoding problem in the native UI headers. With CUDA 13.1.115 and MSVC 19.44, raw UTF-8 strings could become invalid tokens in the CUDA frontend under system code page 936, despite the canonical host `/utf-8` option and UTF-8 console mode. An isolated compile-only case reproduced the failure; the same text written as ASCII universal character escapes in a `u8` literal passed exact UTF-8 byte assertions. Separately, a narrow `"\u25B0"` literal compiled into a question mark. The explicit UTF-8 form produced the correct three bytes.

The repair uses ASCII universal escapes for the foundation and 42 language catalogs in the main program's include closure. All 12,494 catalog literals and 253 foundation literals retain their intended UTF-8 bytes. The 217 ordinary catalog strings containing Unicode now explicitly use `u8`; twenty frame, activity-pulse and sparkline literals also specify UTF-8. Human-readable translation resources remain unchanged. No system locale, compiler-warning rule, CUDA flag, kernel or SASS golden is changed.

The telemetry regression checks independent fixed UTF-8 bytes for all eight frame glyphs, the first activity frame and eight sparkline levels, plus unchanged ASCII alternatives. The existing test compiled with NVCC and ran as a pure CPU program successfully. Linux ON focused regression passed all nine groups, including the 723-check foundation suite and the previously failing root-fusion CLI shell check. Independent review confirmed the literal equivalence and found no actionable defect. The actual Windows TLS ON production build now compiles without Unicode diagnostics; all 67 kernel SASS digests match the original golden. Installation contains exactly three executables with identical build/install hashes; the two helpers have no GPU imports. The complete regression gate and actual Windows executable behavior still need to close before this repair is accepted. Windows production records are `build/p12k-windows-production-final-build-receipt.json` and `build/p12k-windows-production-final-static-receipt.json`. Reproduction and literal-equivalence receipts are retained in `build/p12k-windows-nvcc-encoding-repro.json`, `build/p12k-encoding-source-equivalence.json` and `build/p12k-catalog-source-equivalence.json`.

## Additional system-trust checks

The native one-shot helpers passed five public certificate checks on each platform using system trust: the normal `sha256.badssl.com` endpoint verified, while `expired.badssl.com`, `wrong.host.badssl.com`, `self-signed.badssl.com` and `untrusted-root.badssl.com` returned verification failure during the handshake. The endpoints and their expected test roles were checked against the [official badssl site](https://badssl.com/) and [certificate test configurations](https://github.com/chromium/badssl.com/tree/master/domains/cert).

Each endpoint received one attempt on port 443 with the original ten-second budget. Neither platform used a private CA or trust override, and the probes sent no application bytes. All negative results reached the verification phase; DNS/connect errors or timeouts were not counted as certificate rejection. The shared error code reports aggregate verification failure, not a specific native certificate-error reason. This is ten passing handshake checks, not public mining-session or revocation-policy qualification. Per-attempt replies, request bytes and helper hashes are retained in `build/p12k-public-trust-windows.json` and `build/p12k-public-trust-linux.json`.
