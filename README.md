# CanaryMiner

CanaryMiner is a GPU miner for **Pearl (PRL)**, written in CUDA C++ and released under the [MIT License](LICENSE).
It mines Pearl only and supports NVIDIA Blackwell GPUs (RTX 50-series, SM120) only. Other GPUs, including Ada (RTX 40-series),
are refused at start-up.

It is a community miner maintained by one person, with an optional developer donation (on by default, one flag to turn
off). This README says plainly what has and has not been tested.

## What is tested

| | Status | Tested by the maintainer |
|---|---|---|
| RTX 5080 (Blackwell) | supported | yes: self-test, pool runs, donation windows, clean stop |
| Other RTX 50-series (e.g. RTX 5070) | supported (same SM120 kernels) | not with this public build; the same kernels run on RTX 5070 in the maintainer's private miner |
| Linux x86-64 | supported | Ubuntu 24.04 (WSL2), RTX 5080, driver 616.92. The binary is built on Ubuntu 22.04 and needs glibc 2.34 or newer |
| Windows 10/11 x64 | supported | Windows 11, RTX 5080, driver 616.92 |
| Pools: F2Pool, Kryptex | qualified | yes, accepted shares with 0 rejects |
| Pool: HeroMiners | preset available | not tested with this public build |
| Other Pearl pools | configurable | not tested |
| HiveOS and other mining distributions | not supported yet | no |

Measured on the RTX 5080: about 213 T MAC/s, the same speed as the maintainer's private miner with the same kernels.
Your numbers depend on your card, clocks and power limit.

Download a prebuilt binary for Linux or Windows from the [Releases page](https://github.com/bucnazty2020/CanaryMiner/releases)
(see [Download](#download)), or build from source.

## Developer donation

CanaryMiner can donate **1% of your productive mining time** to the developer. It is voluntary and **on by default**; you can
turn it off at any time and the miner is exactly as supported without it.

- **Turn it off:** pass `--no-donation`, or set `"donation": false` in the config file. The command-line flag wins over the
  config. When it is off the miner prints `developer donation: off`, never connects for a donation, and writes no donation
  state. The stats endpoint then reports `donation.enabled: false`.
- **What it does when on:** mining runs in 100-minute productive cycles split 99:1 between your payout address and the
  donation address. At a random point inside each cycle your mining pauses between two search batches (never mid-search),
  then the donation window mines on the fixed F2Pool endpoint `pearl.f2pool.com:5500`, preset `f2pool`, as
  `bucnazty90.canaryfee`. This is independent of your pool, account, TLS, proxy, and compression settings. The F2Pool account
  `bucnazty90` pays out to the donation address below. After about one minute the miner reconnects with your original settings
  and carries on. Each switch costs a reconnect and a job preparation (about 1-2 seconds measured on an RTX 5080).
- Donation address (public PRL address): `prl1p9cspyppry9vh38rs78k8s5n44qy9k9jn25pl7wwsvj4emegu9shssjs2jc`
  Its SHA-256: `7a805305cf1f57d4512dc9d4594fc5936c09d9576c71b04ab32c0d7ce58420b2`. `canary-miner version` prints the digest for the
  destination a binary uses, so you can check it. The address is set in `src/product/fee.h`; a build may override it with
  `-DCANARY_FEE_DESTINATION`. A build compiled without one never donates.
- Your shares and the donation shares are never mixed: the stats endpoint keeps them under `donation.outcomes`, separate
  from your device and total counters. `donation.pool` and `donation.login` report the fixed donation endpoint and login.
- Only productive time counts: time spent connecting, without a job, or stopped never accrues. Progress is saved in
  `donation-state.json` (in the log directory, or the state directory when no log directory is set); a damaged file is
  ignored with a warning and a fresh cycle starts.
- If the donation pool is unreachable or sends no work for 60 seconds, the window is abandoned, your mining resumes at once,
  and the unmet time is carried as debt (never more than one extra minute). The window is retried after 10 minutes. A
  failure never stops your mining and never counts as a failure of your own pool connection. If you stop the miner during a
  window, the unmet part is carried over to the next start.

Running with the donation off is official and supported; it is still CanaryMiner. See [TRADEMARK.md](TRADEMARK.md) for what a
renamed fork must call itself.

## Download

Prebuilt binaries for RTX 50-series GPUs are on the [Releases page](https://github.com/bucnazty2020/CanaryMiner/releases),
starting with v1.0.1 (v1.0.0 is a source-only release):

- **Linux x86-64:** `canaryminer-<version>-linux-x86_64.tar.gz`. It needs glibc 2.34 or newer (Ubuntu 22.04, 24.04 and later).
- **Windows 10/11 x64:** `canaryminer-<version>-windows-x86_64.zip`. The binary is not code-signed, so Windows SmartScreen or
  your antivirus may warn on first start. Check the checksum below before you allow it.

Both need an NVIDIA driver that supports CUDA 13.1 (tested with driver 616.92). The CUDA toolkit is not needed. Each archive
has a `QUICKSTART.txt`.

Check the download against `SHA256SUMS` from the same release:

```bash
sha256sum -c SHA256SUMS
```

On Windows (PowerShell): `Get-FileHash <file> -Algorithm SHA256` and compare with the line in `SHA256SUMS`.

The binaries are built from the tagged source: Linux on Ubuntu 22.04 with g++ 11.4 and CUDA 13.1.115, Windows with MSVC
19.44 and CUDA 13.1.115. Rebuilds are not yet byte-identical (nvcc embeds temporary file names), so a rebuild will not match
the published checksum exactly.

## Build from source

You need:

- CMake 3.31 or newer;
- the **CUDA 13.1** toolkit (the natives are built and verified against this version), with `nvcc` on the path or passed
  as `-DCMAKE_CUDA_COMPILER`;
- on Linux: g++ 11 or newer; on Windows: Visual Studio 2022 Build Tools (Desktop C++) and Ninja, with `CUDA_PATH`
  pointing at the CUDA 13.1 install.

Linux:

```bash
cmake --preset production-sm120 -DCMAKE_CUDA_COMPILER=/path/to/nvcc
cmake --build --preset production-sm120
```

The binary is `build/production-sm120/canary-miner`.

Windows (from an "x64 Native Tools Command Prompt for VS 2022"):

```bat
cmake --preset production-sm120-windows
cmake --build --preset production-sm120-windows
```

The binary is `build\production-sm120-windows\canary-miner.exe`.

Host-side tests need no GPU and no CUDA toolkit:

```bash
cmake --preset tests
cmake --build --preset tests --parallel
ctest --preset tests
```

The full verification script is `tools/test_all.sh`. It also needs the pinned CUDA toolkit and verifier paths; see
[CONTRIBUTING.md](CONTRIBUTING.md).

## Quick start

1. Copy the example configuration and set your public PRL payout address:

   ```bash
   cp config/config.example.json config.json
   ```

   Edit `wallet` in `config.json`. Use a public payout address only; never put wallet secrets or passwords in the file.

2. Check your GPUs and run the offline self-test:

   ```bash
   ./canary-miner list-devices
   ./canary-miner selftest
   ```

3. Start mining:

   ```bash
   ./canary-miner --config config.json
   ```

   Run `./canary-miner --help` for every option. The stats endpoint is loopback-only at `127.0.0.1:8085` by default
   (`/api/v1/stats` and `/api/v1/version`).

   Share outcomes keep pool verdicts separate from local handling: `rejected` counts pool rejections only; `stale` counts
   shares the pool marked stale; `local_stale` counts candidates retired or superseded before sending; `submit_failed` counts
   delivery failures; and `host_invalid` counts candidates that failed local verification. These local counters do not increase
   the pool-rejection count.

Pool presets: `f2pool` and `kryptex` are qualified (tested with accepted shares and 0 rejects). On F2Pool, set your F2Pool
account name as the pool's `account`; the login becomes `account.worker`. `herominers` is available but not tested with
this build. Other Pearl pools can be configured explicitly but are not qualified.

On Windows, use `canary-miner.exe` (or `canaryminer.bat`) instead of `./canary-miner`. Ctrl-C or closing the console
window stops the miner cleanly and saves its state.

## Third-party software

CanaryMiner contains and was built with third-party material under its own licences. See [THIRD_PARTY.md](THIRD_PARTY.md).

## Documentation

- [CONTRIBUTING.md](CONTRIBUTING.md): gates, sign-off and the no-hand-SASS rule
- [SECURITY.md](SECURITY.md): how to report a vulnerability
- [TRADEMARK.md](TRADEMARK.md): use of the CanaryMiner name and logo
