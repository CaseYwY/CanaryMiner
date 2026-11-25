CanaryMiner TLS candidate

This opt-in candidate is for local validation. manifest.json records the source
revision, platform, TLS backend and SHA-256 of every payload file.

Keep these three programs in the same bin directory:
  canary-miner
  canary_preferences_worker
  canary_pool_tls_stream_worker
A v3 market candidate also keeps canary_public_json_worker in bin.
Windows adds .exe to each program name.

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

In the v2 and v3 candidates, keep bin/canary-tzdb-2026e with the
programs. It contains the verified data used by named tariff clocks.
All profiles support offline market archive replay.

The v3 candidate reports public-https=opt-in in the version command.
Public-ticker settings and economic mode start disabled. One-shot market
commands require explicit flags for each invocation. Actual public-source
validation and permission to distribute this build are separate gates.

See THIRD_PARTY.md and LICENSES for provenance and notices. GPU and mining
acceptance and distribution approval are separate from this candidate check.
