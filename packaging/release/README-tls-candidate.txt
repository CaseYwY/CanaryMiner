CanaryMiner TLS candidate

This opt-in candidate is for local validation. manifest.json records the source
revision, platform, TLS backend and SHA-256 of every payload file.

Keep these three programs in the same bin directory:
  canary-miner
  canary_preferences_worker
  canary_pool_tls_stream_worker
A candidate with public HTTPS also keeps canary_public_json_worker in bin.
A candidate with the energy runtime adds two fixed siblings in bin:
  canary_energy_authorization_worker
  canary_energy_collector_worker
Windows adds .exe to each program name.

Use the package SHA-256 from a trusted channel. Extract into a new directory.
Start with ./bin/canary-miner version (.\bin\canary-miner.exe version on Windows).
Copy config.tls.example.json to a private config, then review it in a terminal:
  ./bin/canary-miner settings --config FILE
  .\bin\canary-miner.exe settings --config FILE  (Windows PowerShell)

The example requests strict TLS on the first user pool and disables donation.
kryptex-tls selects the US server. For Hong Kong use kryptex-hk-tls or
herominers-hk-tls. See canary-miner --help for the full list of regions.
Strict TLS uses the system CA store, requires a matching DNS name and
disables plaintext fallback.

Windows uses Schannel and requires the x64 Visual C++ 2015–2022 runtime
at least as recent as the build toolset. Linux uses the static OpenSSL
version recorded in manifest.json and the system CA store. The 4.1.0
build verifies intermediate OCSP responses supplied during TLS 1.3.
Linux needs the glibc floor in manifest.json, compatible C++ runtime
libraries, and kernel pidfd support.

When the candidate includes named tariff clocks, keep bin/canary-tzdb-2026e
with the programs. It contains the verified data used by named tariff clocks.
All profiles support offline market archive replay.

A public HTTPS candidate reports public-https=opt-in in the version command.
The public commands accept --dns-server cloudflare-doh for a one-shot
encrypted DNS lookup with the installed public worker.
Public-ticker settings and economic mode start disabled. One-shot market
commands require explicit flags for each invocation. Actual public-source
validation and permission to distribute this build are separate gates.

An energy runtime candidate reports energy-runtime=opt-in in the version command.
Energy persistence starts off and requires saved device enrollment and a new
explicit mining parent. For read-only journal reporting use energy-periods
--help. The collector loads system SQLite 3.31.0 or later at runtime. Use
Windows System32 winsqlite3.dll or the distribution libsqlite3.so.0 on
Linux. Use a serviced system library and protect the application and state
directories. GPU-board history and whole-meter billing have separate inputs.

A billing watch candidate adds deploy/Register-BillingWatch.ps1 on
Windows or deploy/billing-watch.sh on Linux. These tools use the installed
miner to watch a private Octopus billing workspace on login. Installation
creates no task or service. See "Run the watcher at login" in the repository's
docs/tariff-declaration.md for setup, status and removal.

A v6 Octopus account candidate includes the billing watch helper and
tools/capture_octopus_consumption.py plus its local Python modules.
Run python -I -B tools/capture_octopus_consumption.py --help to see the
one-shot capture options. The tool asks for account details and the API key
at masked terminal prompts. See "Capture readings from an Octopus account"
in docs/tariff-declaration.md for the private workspace handoff.

See THIRD_PARTY.md and LICENSES for provenance and notices. GPU and mining
acceptance and distribution approval are separate from this candidate check.
