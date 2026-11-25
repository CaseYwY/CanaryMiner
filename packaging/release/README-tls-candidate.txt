CanaryMiner TLS candidate

This opt-in candidate is for local validation. manifest.json records the source
revision, platform, TLS backend and SHA-256 of every payload file.

Keep all three programs in the same bin directory:
  canary-miner
  canary_preferences_worker
  canary_pool_tls_stream_worker
Windows adds .exe to each name.

Use the package SHA-256 from a trusted channel. Extract into a new directory.
Start with ./bin/canary-miner version (.\bin\canary-miner.exe version on Windows).
Copy config.tls.example.json to a private config, then review it in a terminal:
  ./bin/canary-miner settings --config FILE
  .\bin\canary-miner.exe settings --config FILE  (Windows PowerShell)

The example requests strict TLS on the first user pool and disables donation.
Use kryptex-tls for US or kryptex-hk-tls for Hong Kong. Strict TLS requires
system trust and a matching DNS name. The strict presets disable plaintext
fallback. Each installation uses system trust rather than a bundled CA file.

Windows uses Schannel and requires the x64 Visual C++ 2015–2022 runtime
at least as recent as the build toolset. Linux uses static OpenSSL 3.5.9
and the system CA store. Linux needs the glibc floor in manifest.json,
compatible C++ runtime libraries, and kernel pidfd support.

If the manifest schema is canaryminer-tls-named-tzdb-candidate-v2, keep
the bin/canary-tzdb-2026e directory with the programs. It contains the
verified data required by named tariff clocks in meter-bill. Both package
profiles support offline market archive replay and leave economic mode
disabled on first use.

See THIRD_PARTY.md and LICENSES for provenance and notices. GPU and mining
acceptance and distribution approval are separate from this candidate check.
