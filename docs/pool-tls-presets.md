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

Logs are retained as `build/p12k-linux-{on,off}-{build,ctest,test-details}.log`. An initial OFF test-launch command failed at shell parsing before running any tests; that failure is retained separately as `build/p12k-linux-off-ctest-launch-error.log`. The subsequent direct test invocation produced the six passing results. Full-gate validation of this new source and Windows production-artifact qualification remain pending.

The earlier public endpoint checks established system-trust handshakes on Windows and Linux with zero application bytes. They did not authenticate a wallet, obtain a mining job or establish proof acceptance. P-12 and release qualification remain incomplete.
