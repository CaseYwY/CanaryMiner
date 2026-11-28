# Windows and Linux TLS release candidates

The candidate tool packages an enabled production SM120 build for installation and validation. The established release scripts still use the ordinary TLS-off policy. The base inventory has the miner, preferences worker and TLS stream worker together in `bin/`.

## Build inputs and archive creation

Build the `production-sm120` profile in Release mode with CUDA 13.1.115 and `CANARY_ENABLE_POOL_TLS_RUNTIME=ON`. Use a neutral build and toolkit path. Keep the original source revision, CMake cache, toolchain and output hashes in the build record. Pass that exact build directory and source revision to the packer. The release candidate archive records this declared revision and each file hash. Verify the revision against the external build record when accepting a candidate.

For Linux, use the inventoried static OpenSSL 4.1.0 build to enable [TLS 1.3 intermediate certificate status](pool-native-tls.md#linux-certificate-status-in-the-handshake). The installer also accepts candidates built with OpenSSL 3.5.9. Supply the unmodified `LICENSE.txt` from the selected release. The packer binds the version in the CMake cache to the manifest, licence path and TLS worker. It checks the exact licence hash and the linked version string. Archive verification repeats the version and licence checks. The packer requires the system x86-64 ELF loader and rejects unexpected shared libraries and runtime search paths. For Windows, use the configured MSVC linker's sibling `dumpbin.exe`. The Windows candidate uses Schannel and depends on the supported x64 Visual C++ runtime. [OpenSSL releases](https://openssl-library.org/source/)

```sh
python3 -B packaging/release/tls_candidate.py pack --platform linux \
  --build BUILD_DIR --revision SOURCE_COMMIT --deny-file PRIVATE_DENY_FILE \
  --tool /usr/bin/objdump --openssl-license OPENSSL_SOURCE/LICENSE.txt \
  --archive canaryminer-tls-candidate-linux-x86_64.tar.gz
```

On Windows use `--platform windows`, the configured `dumpbin.exe`, and an archive ending in `.zip`. The packer reads the CMake cache and requires the enabled capabilities to match the selected candidate profile. It checks each complete image without running it. Only the miner role may import the GPU driver. Linux also checks the native pidfd symbol contract and the highest glibc requirement. Every included image passes the private-content scan. The tool re-reads the written archive before making it available at the requested new path.

The archive carries the strict TLS example, candidate instructions, project and component licences and a manifest of payload sizes and SHA-256 digests. Private build records and trust material stay outside the package. The archives contain no separate OpenSSL, CUDA driver or VC runtime installers.

## Candidate with named tariff clocks

The optional named tariff clock profile also packages the reviewed IANA 2026e tariff clock data.
Configure with `CANARY_ENABLE_NAMED_TZDB=ON`, the reviewed static date-tz build via `date_DIR`, and
`CANARY_BUNDLED_TZDB_DIR` pointing to the verified data described in [Electricity core](electricity-core.md#local-installation-of-the-reviewed-tariff-time-data).
Add `--with-named-tzdb` to the pack command. The packer reads the staged `canary-tzdb-2026e` data
from the same build directory as the three binaries and compares it to the native runtime pins.
It installs the data as regular files under `bin/canary-tzdb-2026e`, next to the miner. The Windows package
includes `windowsZones.xml` as required by its backend.

The verifier recognizes the candidate schemas automatically. It checks the named data pins and
looks for those pins in both native consumers, the miner and the preferences worker. It also checks
the miner's compiled reference to the TLS stream worker. Use the updated verifier to install a named-clock package.
Keep the directory beside the miner when moving the installation. You can use the installed
[`meter-bill` command](tariff-declaration.md) with named tariff and reset clocks and reopen existing local market hour
archives with [`market-hour --open-review`](market-reference-runtime.md#save-and-replay-a-public-hour).
The v1 and v2 profiles use a build with public HTTPS disabled. To make the original TLS-only candidate,
also disable the named backend and omit `--with-named-tzdb`.

## Candidate with opt-in public HTTPS

Build the named tariff profile with `CANARY_ENABLE_PUBLIC_HTTPS=ON` and supply the reviewed static libcurl
8.23.0 and c-ares 1.34.9 installation through `CMAKE_PREFIX_PATH`. Use Schannel on Windows and build
libcurl with the same inventoried static OpenSSL version as the pool TLS worker on Linux. Set `CMAKE_SKIP_RPATH=ON` on Linux
so the candidate uses system library paths. Build c-ares with `CARES_STATIC=ON` and
`CARES_SHARED=OFF`. Build libcurl with `BUILD_STATIC_LIBS=ON`, `BUILD_SHARED_LIBS=OFF`, `ENABLE_ARES=ON`,
`CURL_DISABLE_LDAP=ON` and `CURL_DISABLE_LDAPS=ON`. Supply c-ares to libcurl through its CMake prefix and omit
third-party protocol, decompression, HTTP/2 and SSH dependencies. Use the system CA paths on Linux and Schannel's
system trust on Windows. See [dependency provenance](public-https.md#development-dependency-provenance-not-approved-release-locks)
and [curl's source build instructions](https://curl.se/docs/install.html).

Add `--with-named-tzdb --with-public-https` to the pack command. A v3 candidate has four programs in `bin/`,
including `canary_public_json_worker`. The packer checks each native dependency table and the worker's compiled
backend and version strings. Windows permits IP Helper imports for this worker's resolver. The candidate
does not include a separate curl, c-ares or OpenSSL shared library. The verifier checks the expected inventory,
build markers and corresponding notices. The `version` command prints `public-https=opt-in` on this build.

One-shot requests still require explicit flags for each invocation. A Dashboard launch requires a saved
public ticker request and permission for that launch. See [public market commands](market-reference-runtime.md#check-public-tickers-without-mining).
The package can be installed and verified offline. Live provider use and distribution approval remain separate.

## Verification and installation

Use the SHA-256 returned by `pack` through a trusted channel. The manifest checks individual files and the expected archive hash binds that manifest to the published candidate bytes.

```sh
python3 -B packaging/release/tls_candidate.py verify --platform linux \
  --archive canaryminer-tls-candidate-linux-x86_64.tar.gz --sha256 ARCHIVE_SHA256
python3 -B packaging/release/tls_candidate.py install --platform linux \
  --archive canaryminer-tls-candidate-linux-x86_64.tar.gz --sha256 ARCHIVE_SHA256 \
  --prefix "CanaryMiner TLS candidate"
```

Installation writes to a new directory after the complete verification step. The verifier checks the exact archive inventory, duplicates, links, sizes and modes. Installation re-reads every output and checks Linux executable permissions. Use a POSIX filesystem for the Linux package and a filesystem supporting hard links when creating either package. A failed interrupted install can leave its new directory for inspection. Choose a fresh directory for another attempt.

Use the extracted miner's `version` and interactive `settings` commands for offline CPU-only acceptance.
Use [`pool-tls-check`](pool-tls-runtime.md#check-a-pool-before-mining) for a single explicitly selected endpoint with the installed TLS worker. The examples request strict TLS for the first user pool and turn donation off. Qualified native GPU, real pool login and proof submission, donation TLS and redistribution acceptance are separate release checks.

Settings keeps economic mode off for fresh installations and old preferences that have no economic mode field. An explicitly saved request remains selected on reopening Settings.

## Version 4 energy runtime layout

Build with `CANARY_ENABLE_ENERGY_RUNTIME=ON` and add `--with-energy-runtime` to `tls_candidate.py pack`.
The v4 manifest declares whether named tariff clocks are bundled and records the system SQLite
requirement. A TLS energy candidate may also carry the existing public HTTPS worker when that
capability and the named clock data are explicitly selected. Candidates with all three optional
capabilities have six native programs and 37 hashed payload files.

Keep the energy authorization and collector workers in the same `bin` directory as the miner.
Only the miner may import the GPU driver. The collector loads the platform SQLite library at
runtime. The installer verifies the archive, file hashes and permissions. Clean-run tests exercise
SQLite by opening an enrolled journal with the installed helper. SQLite remains a serviced
system dependency and is absent from the archive.

`version` reports `energy-runtime=opt-in`. Saved enrollment remains off by default. Use the existing
[energy authorization controls](energy-runtime.md) and [monthly journal report](energy-periods.md).
Archive verification cannot establish physical sensor accuracy or whole-meter coverage.
The official release path retains its default-off build and refuses energy-enabled artifacts.

## Version 5 billing watch layout

Add `--with-billing-watch --with-named-tzdb` to the candidate pack command. The v5 manifest
adds one file for the target platform. It verifies `deploy/Register-BillingWatch.ps1`
on Windows or the executable `deploy/billing-watch.sh` on Linux. Public HTTPS and the
energy runtime are independent options. A v5 candidate with both options has six native
programs and 38 hashed payload files.

Extraction and installation only copy files. Follow the [billing watch deployment guide](tariff-declaration.md#run-the-watcher-at-login)
to register a Windows logon task or enable a Linux systemd user service. The v5 installer
also verifies the earlier v1 through v4 profiles.

## Version 6 Octopus account capture layout

Add `--with-octopus-account --with-named-tzdb` to include the billing watcher
deployment helper and three Python scripts under `tools/`. With both optional
native runtimes selected, each v6 candidate has six native programs and 41 hashed
payload files. The capture uses the machine's Python 3 and its effective TLS trust.
The candidate installation only copies the scripts. Follow the
[Octopus account capture guide](tariff-declaration.md#capture-readings-from-an-octopus-account)
for explicit interactive capture and handoff to the billing watcher.

## Candidate evidence

### Version 3 clean installation

The opt-in public HTTPS candidates use source revision `c0999a0e4ccb452fbe5072e25fbcf377e77303e1`.
[PR #38](https://github.com/CaseYwY/CanaryMiner/pull/38)

The archive in the disconnected Windows Server 2025 Standard Evaluation guest had SHA-256
`be24cdd1befe17bbef97487057d9f879c1a1300ec598dc333710e6c20df85ce5`.

The clean Windows installation re-read all 35 payload files and matched their sizes and SHA-256
digests. The miner reported `public-https=opt-in`. The three installed workers and both public market
commands refused invocation with no arguments. Through the installed Schannel pool worker, a protocol
probe authorized and submitted synthetic work over TLS 1.2 and 1.3 to a local fixture. The guest used
a temporary root and its signed revocation list. With a revoked certificate, both protocol versions
reported verification failure before sending any Stratum application data. Each run removed the
temporary root and revocation list from the guest.

The Debian 13 guest booted from a fresh system image with loopback as its only network interface.
For archive
SHA-256 `7eedd06ed3096d408514237c7cdd0a74855f300bd2771f6c9846b84656034eb6`, it verified
all 35 installed payloads, executable permissions for all four programs and their link-time shared
libraries. The miner, three workers and both public market commands produced the same startup and
refusal results.
The installed Settings verified Hong Kong TLS migration and the default-off economic mode.
The local pool cases verified strict refusal, explicitly consented plaintext fallback, refusal after
a handshake error and rejection of an untrusted peer. The installed worker accepted a trusted
local fixture over TLS 1.2 and 1.3.

Live market data, a real pool session, GPU operation, donation TLS endpoint qualification and
distribution approval remain open for this candidate.

See [TLS installation layout](pool-tls-runtime-install.md), [runtime policy](pool-tls-runtime.md), [OpenSSL 3.5 notices](https://github.com/openssl/openssl/blob/openssl-3.5.9/LICENSE.txt), and [Microsoft supported Visual C++ Redistributable downloads](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).
