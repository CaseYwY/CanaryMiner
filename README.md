# CanaryMiner

CanaryMiner is a GPU miner for **Pearl (PRL)**, written in CUDA C++ and released under the [MIT License](LICENSE).
It mines Pearl only and supports NVIDIA Blackwell GPUs (RTX 50-series, SM120) only:

| Architecture | Example cards | Status |
|---|---|---|
| Blackwell, SM120 | RTX 5080, RTX 5070 | supported |

Other GPUs, including Ada, are refused at start-up (the device allowlist fails closed).

> **Pre-release.** There are no release binaries yet. Build from source as described below.

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
  and carries on. Each switch costs a reconnect and a job preparation.
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

## Build from source

You need:

- CMake 3.31 or newer;
- the **CUDA 13.1** toolkit (the natives are built and verified against this version), with `nvcc` on the path or passed
  as `-DCMAKE_CUDA_COMPILER`;
- a Linux host. A Windows build has not been verified yet.

Build the native for your GPU architecture:

```bash
cmake --preset production-sm120 -DCMAKE_CUDA_COMPILER=/path/to/nvcc
cmake --build --preset production-sm120
```

The binary is `build/production-sm120/canary-miner`.

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

Pool presets `kryptex` and `herominers` are qualified. Other Pearl pools can be configured explicitly but are not
qualified.

## Third-party software

CanaryMiner contains and was built with third-party material under its own licences. See [THIRD_PARTY.md](THIRD_PARTY.md).

## Documentation

- [CONTRIBUTING.md](CONTRIBUTING.md): gates, sign-off and the no-hand-SASS rule
- [SECURITY.md](SECURITY.md): how to report a vulnerability
- [TRADEMARK.md](TRADEMARK.md): use of the CanaryMiner name and logo
