# Windows and Linux TLS release candidates

The candidate tool packages an enabled production SM120 build for installation and validation. The established release scripts still use the ordinary TLS-off policy. A candidate uses three fixed siblings in `bin/`: the miner, the preferences worker and the TLS stream worker.

## Build inputs and archive creation

Build the `production-sm120` profile in Release mode with CUDA 13.1.115 and `CANARY_ENABLE_POOL_TLS_RUNTIME=ON`. Use a neutral build and toolkit path. Keep the original source revision, CMake cache, toolchain and output hashes in the build record. Pass that exact build directory and source revision to the packer. The release candidate archive records this declared revision and each file hash. Verify the revision against the external build record when accepting a candidate.

The Linux candidate uses the inventoried static OpenSSL 3.5.9 build. Supply the unmodified `LICENSE.txt` from the verified 3.5.9 source. The tool checks the exact licence hash and TLS helper version marker. It requires the system x86-64 ELF loader and rejects unexpected shared-library dependencies and runtime search paths. For Windows, use the configured MSVC linker's sibling `dumpbin.exe`. The Windows candidate uses Schannel and depends on the supported x64 Visual C++ runtime.

```sh
python3 -B packaging/release/tls_candidate.py pack --platform linux \
  --build BUILD_DIR --revision SOURCE_COMMIT --deny-file PRIVATE_DENY_FILE \
  --tool /usr/bin/objdump --openssl-license OPENSSL_SOURCE/LICENSE.txt \
  --archive canaryminer-tls-candidate-linux-x86_64.tar.gz
```

On Windows use `--platform windows`, the configured `dumpbin.exe`, and an archive ending in `.zip`. The packer reads the CMake cache and requires the enabled capabilities to match the selected candidate profile. It checks each complete image without running it. Only the miner role may import the GPU driver. Linux also checks the native pidfd symbol contract and the highest glibc requirement. All three images pass the existing private-content scan. The tool re-reads the written archive before making it available at the requested new path.

The archive carries the strict TLS example, candidate instructions, project and component licences and a manifest of payload sizes and SHA-256 digests. Private build records and trust material stay outside the package. The archives contain no separate OpenSSL, CUDA driver or VC runtime installers.

## Candidate with named tariff clocks

The optional named tariff clock profile also packages the reviewed IANA 2026e tariff clock data.
Configure with `CANARY_ENABLE_NAMED_TZDB=ON`, the reviewed static date-tz build via `date_DIR`, and
`CANARY_BUNDLED_TZDB_DIR` pointing to the verified data described in [Electricity core](electricity-core.md#local-installation-of-the-reviewed-tariff-time-data).
Add `--with-named-tzdb` to the pack command. The packer reads the staged `canary-tzdb-2026e` data
from the same build directory as the three binaries and compares it to the native runtime pins.
It installs the data as regular files under `bin/canary-tzdb-2026e`, next to the miner. The Windows package
includes `windowsZones.xml` as required by its backend.

The verifier recognizes both manifest schemas automatically. It checks the named data pins and
looks for those pins in both native consumers, the miner and the preferences worker. It also checks
the miner's compiled reference to the TLS stream worker. Use the updated verifier to install a named-clock package.
Keep the directory beside the miner when moving the installation. You can use the installed
[`meter-bill` command](tariff-declaration.md) with named tariff and reset clocks and reopen existing local market hour
archives with [`market-hour --open-review`](market-reference-runtime.md#save-and-replay-a-public-hour).
Both candidate profiles keep public HTTPS disabled. To make the original TLS-only candidate, use the
TLS build without the named backend and omit `--with-named-tzdb`.

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

Use the extracted miner's `version` and interactive `settings` commands for CPU-only acceptance. The examples request strict TLS for the first user pool and turn donation off. Qualified native GPU, real pool login and proof submission, donation TLS and redistribution acceptance are separate release checks.

Settings keeps economic mode off for fresh installations and old preferences that have no economic mode field. An explicitly saved request remains selected on reopening Settings. This candidate disables the public HTTPS transport at build time.

## Candidate evidence

See [TLS installation layout](pool-tls-runtime-install.md), [runtime policy](pool-tls-runtime.md), [OpenSSL 3.5 notices](https://github.com/openssl/openssl/blob/openssl-3.5.9/LICENSE.txt), and [Microsoft supported Visual C++ Redistributable downloads](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).
