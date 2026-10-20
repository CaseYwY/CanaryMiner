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

### Measured speed and power

| | RTX 5080 |
|---|---|
| Hashrate | about 200 T MAC/s (median 201.5; 10-minute average 197.0) |
| Board power | about 240 W (median 240.3 W; average 235.4 W), as reported by the driver |
| Efficiency | about 0.84 T MAC/s per watt |
| Settings | core clock 2655 MHz, memory clock 7001 MHz, power limit 389 W (not reached), fans 80% |
| Setup | v1.0.2 release build, Windows 11, driver 616.92, Kryptex, donation off |

Measured over 10 minutes, after a 5-minute warm-up, with samples every 15 seconds from the miner's stats endpoint and
`nvidia-smi`. Board power is the GPU's own reading, not wall power. Your numbers depend on your card, clocks, power limit
and cooling.

Download a prebuilt binary for Linux or Windows from the [Releases page](https://github.com/bucnazty2020/CanaryMiner/releases)
(see [Download](#download)), or build from source.

## Contents

- [Download](#download)
- [Windows guide](#windows-guide)
- [Linux guide](#linux-guide)
- [Pools, wallets and workers](#pools-wallets-and-workers)
- [Several GPUs](#several-gpus)
- [GPU tuning (clocks, power, fans)](#gpu-tuning-clocks-power-fans)
- [The dashboard](#the-dashboard)
- [Config file](#config-file)
- [Stats endpoint](#stats-endpoint)
- [Share counters](#share-counters)
- [Developer donation](#developer-donation)
- [Troubleshooting](#troubleshooting)
- [Build from source](#build-from-source)

Short how-tos for everyday tasks (changing pools and wallets, tuning, general use) are in [HELP.txt](HELP.txt), which is
also in every release archive.


## Download

Prebuilt binaries for RTX 50-series GPUs are on the [Releases page](https://github.com/bucnazty2020/CanaryMiner/releases),
starting with v1.0.2 (v1.0.0 and v1.0.1 are source-only releases):

- **Linux x86-64:** `canaryminer-<version>-linux-x86_64.tar.gz`. It needs glibc 2.34 or newer (Ubuntu 22.04, 24.04 and later).
- **Windows 10/11 x64:** `canaryminer-<version>-windows-x86_64.zip`. The binary is not code-signed, so Windows SmartScreen or
  your antivirus may warn on first start. Check the checksum below before you allow it.

Both need an NVIDIA driver that supports CUDA 13.1 (tested with driver 616.92). The CUDA toolkit is not needed. Each archive
has a `README.txt` (quick start for that system) and `HELP.txt` (everyday tasks).

Check the download against `SHA256SUMS` from the same release:

```bash
sha256sum -c SHA256SUMS
```

On Windows (PowerShell): `Get-FileHash <file> -Algorithm SHA256` and compare with the line in `SHA256SUMS`.

The binaries are built from the tagged source: Linux on Ubuntu 22.04 with g++ 11.4 and CUDA 13.1.115, Windows with MSVC
19.44 and CUDA 13.1.115. Rebuilds are not yet byte-identical (nvcc embeds temporary file names), so a rebuild will not match
the published checksum exactly.

## Windows guide

### Requirements

- Windows 10 or 11, 64-bit, and an NVIDIA RTX 50-series GPU.
- An NVIDIA driver that supports CUDA 13.1 (616.92 or newer was tested).
- The Microsoft Visual C++ 2015-2022 x64 runtime. If `canary-miner.exe` reports a missing DLL, install
  "Microsoft Visual C++ Redistributable (x64)" from Microsoft.

### Install and first start

1. Download `canaryminer-<version>-windows-x86_64.zip` and check its SHA-256 (see [Download](#download)).
2. Right-click the zip, choose **Extract All**, and pick a folder you own, for example `C:\CanaryMiner`. Run the miner from
   the extracted folder, not from inside the zip.
3. Right-click `start.bat`, choose **Edit**, and replace `YOUR_PRL_ADDRESS` with your public PRL payout address (it starts
   with `prl1`). The `.rig1` after it is your rig name on the pool. Save.
4. Double-click `start.bat`. If Windows shows "Windows protected your PC", click **More info**, then **Run anyway**.
5. The dashboard appears. Allow about 5 minutes for the hashrate to stabilize.
6. Stop with **Ctrl+C**, the **Q** key, or by closing the window. The miner stops cleanly and saves its state.

### Files in the Windows archive

| File | What it is |
|---|---|
| `canary-miner.exe` | the miner |
| `start.bat` | basic launcher (Kryptex); edit your PRL address and double-click |
| `start-kryptex.bat`, `start-herominers.bat` | the same, pre-filled for each pool |
| `start-f2pool.bat` | F2Pool; edit your F2Pool account name (not a PRL address) |
| `start-advanced.bat` | settings at the top (wallet, worker, pool, donation, extra options) and an automatic restart after the miner stops |
| `config.example.json` | example config file (see [Config file](#config-file)) |
| `README.txt`, `HELP.txt` | quick start and everyday tasks |
| `LICENSE.txt`, `THIRD_PARTY.txt`, `LICENSES\` | licences |

### Command line

Open a Command Prompt or PowerShell in the folder and run, for example:

```bat
canary-miner.exe --pool kryptex --wallet YOUR_PRL_ADDRESS.rig1
canary-miner.exe --pool f2pool --wallet YOUR_F2POOL_ACCOUNT.rig1
canary-miner.exe --pool herominers --wallet YOUR_PRL_ADDRESS.rig1 --gpu-id 0 --no-donation
canary-miner.exe list-devices
canary-miner.exe selftest
canary-miner.exe --help
```

### Tuning on Windows

CanaryMiner does not change clocks, power or fans. Set them with MSI Afterburner (or a similar tool) before you start the
miner; see [GPU tuning](#gpu-tuning-clocks-power-fans).

## Linux guide

### Requirements

- x86-64 Linux with glibc 2.34 or newer (Ubuntu 22.04, 24.04 and later), and an NVIDIA RTX 50-series GPU.
- An NVIDIA driver that supports CUDA 13.1 (616.92 or newer was tested). The driver's `libcuda.so.1` is the only NVIDIA
  library the miner needs; the CUDA toolkit is not needed.
- The miner does not need root.

### Install and first start

```bash
tar -xzf canaryminer-<version>-linux-x86_64.tar.gz
cd canaryminer-<version>
nano start.sh          # replace YOUR_PRL_ADDRESS with your prl1... address; keep .rig1 or rename it
./start.sh
```

Allow about 5 minutes for the hashrate to stabilize. Stop with **Ctrl+C** or the **Q** key.

To keep the miner running after you log out of an SSH session, start it inside `tmux` or `screen`:

```bash
tmux new -s canary ./start.sh       # detach with Ctrl+B then D; reattach with: tmux attach -t canary
```

### Files in the Linux archive

| File | What it is |
|---|---|
| `canary-miner` | the miner |
| `start.sh` | basic launcher (Kryptex); edit your PRL address and run |
| `start-kryptex.sh`, `start-herominers.sh` | pre-filled per pool; extra options pass through, for example `./start-kryptex.sh --gpu-id 0` |
| `start-f2pool.sh` | F2Pool; edit your F2Pool account name (not a PRL address) |
| `start-advanced.sh` | settings at the top (wallet, worker, pool, donation, extra options) and an automatic restart; `RESTART=0` runs it once |
| `config.example.json` | example config file (see [Config file](#config-file)) |
| `README.txt`, `HELP.txt` | quick start and everyday tasks |
| `LICENSE`, `THIRD_PARTY.md`, `LICENSES/` | licences |

### Command line

```bash
./canary-miner --pool kryptex --wallet YOUR_PRL_ADDRESS.rig1
./canary-miner --pool f2pool --wallet YOUR_F2POOL_ACCOUNT.rig1
./canary-miner --pool stratum+tcp://prl-us.kryptex.network:7048 --wallet YOUR_PRL_ADDRESS.rig1 --gpu-id 0,1
./canary-miner list-devices
./canary-miner selftest
./canary-miner --help
```

If a file is not executable after extracting, run `chmod +x canary-miner start*.sh`.

### Tuning on Linux

CanaryMiner does not change clocks, power or fans. Set them with `nvidia-smi` (or your mining OS's tool) before you start
the miner; see [GPU tuning](#gpu-tuning-clocks-power-fans).

## Pools, wallets and workers

| Option | Meaning |
|---|---|
| `-o`, `--pool SPEC` | `f2pool`, `kryptex`, `herominers`, `HOST:PORT`, or `stratum+tcp://HOST:PORT` |
| `-u`, `--wallet ADDR[.WORKER]` | your public PRL payout address; the part after the dot is the rig name. On F2Pool use `ACCOUNT.WORKER` instead |
| `--worker NAME` | rig name (overrides the part after the dot) |
| `-p`, `--password P` | pool password field (default `x`) |

| Preset | Host | Status |
|---|---|---|
| `f2pool` | `pearl.f2pool.com:5500` | qualified (accepted shares, 0 rejects) |
| `kryptex` | `prl-us.kryptex.network:7048` | qualified (accepted shares, 0 rejects) |
| `herominers` | `us2.pearl.herominers.com:1200` | available, not tested with this build |

- **Change your wallet:** edit the address after `--wallet` in your start file (or `WALLET` in `start-advanced`), save, and
  restart the miner. Use a public payout address only; never put a seed phrase, private key or password in any file.
- **Change your pool:** use the start file for that pool, or edit `--pool`. Any Pearl pool works as `HOST:PORT`.
- **F2Pool logs in with your F2Pool account name, not a PRL address.** Create an F2Pool account, set your PRL payout
  address in its settings, and pass `--wallet YOUR_F2POOL_ACCOUNT.rig1`. A prl1 address on F2Pool is refused at start-up.
  Kryptex and HeroMiners take your prl1 address directly.
- **Not supported yet:** TLS (`stratum+ssl://`) and pool failover (only the first pool is used).

## Several GPUs

One command mines on every supported GPU. `--gpu-id` picks some of them: `all` (the default), indexes such as `0,1`, a
`GPU-...` UUID, or a PCI bus id. Run `list-devices` to see them.

Each selected GPU runs in its own worker process with its own donation scheduler, state and log folder (`gpu-<index>`).
A worker that exits unexpectedly is restarted after 10 seconds, at most 3 times an hour per GPU; the other GPUs keep mining.
Ctrl-C stops every worker. GPUs that are not RTX 50-series are skipped.

## GPU tuning (clocks, power, fans)

CanaryMiner mines with whatever GPU settings are applied when it starts; it does not change them. Apply your tuning first,
then start the miner. The tuning flags used by other miners (`--gpu-cclock`, `--gpu-mclock`, `--gpu-coffset`,
`--gpu-moffset`, `--gpu-plimit`, `--gpu-fan`, `--oc-delay`, `--oc-reset-only`) are accepted, so existing command lines keep
working, but they are ignored with a notice. Built-in tuning may come back in a later release.

Tuning can make a GPU unstable. Change one setting at a time, watch for errors and rejected shares, and go back to stock if
something goes wrong.

### Windows: MSI Afterburner

1. Install and open MSI Afterburner (or your card maker's tool).
2. Set **Power Limit**, **Core Clock** (offset), **Memory Clock** (offset) and **Fan Speed**.
3. Click **Apply** and save to a profile slot. Optionally enable "Apply overclocking at system startup".
4. For a locked core clock, open the curve editor (Ctrl+F) and flatten the curve at the clock you want.
5. Start CanaryMiner.

### Linux: nvidia-smi

`nvidia-smi` ships with the NVIDIA driver and needs `sudo`. `-i 0` picks GPU 0 (see `nvidia-smi -L`); leave it out to change
every GPU.

```bash
sudo nvidia-smi -pm 1                     # persistence mode
sudo nvidia-smi -i 0 -pl 175              # power limit, watts
sudo nvidia-smi -i 0 -lgc 2400,2400       # lock the core clock, MHz (MIN,MAX)
sudo nvidia-smi -i 0 -lmc 7001,7001       # lock the memory clock, MHz

nvidia-smi --query-gpu=index,power.limit,clocks.sm,clocks.mem,fan.speed --format=csv   # what is applied now

sudo nvidia-smi -i 0 -rgc                 # back to stock: core clock
sudo nvidia-smi -i 0 -rmc                 # back to stock: memory clock
sudo nvidia-smi -i 0 -pl <default>        # default power limit, from: nvidia-smi -q -d POWER
```

Settings last until the next reboot; put them in a script you run before the miner if you want them every time. Clock
offsets and manual fan speed are not available in `nvidia-smi`; they need `nvidia-settings` with an X server and Coolbits,
or your mining OS's tool. Apply offsets only after the base settings are stable, and check the kernel log after a change:

```bash
sudo journalctl -k --since "10 min ago" | grep -i xid
```

## The dashboard

On a terminal the miner shows a live dashboard: per GPU hashrate (TH/s, the same figure as T MAC/s in the stats endpoint),
power, temperature, fan, clocks and shares, a donation row and recent events. Keys: **Q** quit, **D** details (PCI bus, pool
state, restarts, last error per GPU), **L** log. `--no-dashboard`, or output that is not a terminal, gives plain timestamped
lines. Allow about 5 minutes after a start for the hashrate to stabilize.

## Config file

Settings can live in a JSON file instead of the command line. Copy `config.example.json` to `config.json`, edit it, and
start with `--config config.json`. Command-line options win over the file.

| Key | Meaning |
|---|---|
| `wallet` | your public PRL payout address |
| `worker` | rig name |
| `pools` | a list; the first entry is used: a preset (`"kryptex"`), `"HOST:PORT"`, or an object; F2Pool needs `{"url": "f2pool", "account": "YOUR_F2POOL_ACCOUNT"}` |
| `devices` | `["all"]`, or indexes, UUIDs or PCI bus ids |
| `api` | stats endpoint: `enabled`, `bind` (loopback only), `port` |
| `log` | `dir`, `max_file_bytes`, `max_files`, `redact_worker` |
| `state_dir` | where the miner keeps its state |
| `donation` | `true` (default) or `false` |
| `shutdown.drain_seconds` | how long a stop waits for work in flight (1-600, default 10) |

The full schema is [config/config-v1.schema.json](config/config-v1.schema.json).

## Stats endpoint

The miner serves JSON on loopback only, at `127.0.0.1:8085` by default: `/api/v1/stats` and `/api/v1/version`.
`--api-enable` moves it to `127.0.0.1:21550` unless you give `--api-port N`; `--api-port 0` turns it off. The stats report one
`devices[]` entry per GPU plus totals, and keep donation shares under `donation.outcomes`, separate from yours.

## Share counters

Share outcomes keep pool verdicts separate from local handling: `rejected` counts pool rejections only; `stale` counts
shares the pool marked stale; `local_stale` counts candidates retired or superseded before sending; `submit_failed` counts
delivery failures; and `host_invalid` counts candidates that failed local verification. These local counters do not
increase the pool-rejection count.

## Developer donation

CanaryMiner can donate **1% of your productive mining time** to the developer. It is voluntary and **on by default**; you can
turn it off at any time and the miner is exactly as supported without it.

- **Turn it off:** pass `--no-donation`, or set `"donation": false` in the config file. The command-line flag wins over the
  config. When it is off the miner prints `developer donation: off`, never connects for a donation, and writes no donation
  state. The stats endpoint then reports `donation.enabled: false`.
- **What it does when on:** mining runs in 100-minute productive cycles split 99:1 between you and the developer. At a
  random point inside each cycle your mining pauses between two search batches (never mid-search), then about one minute is
  mined on F2Pool (`pearl.f2pool.com:5500`) to the developer's F2Pool account. This is independent of your own pool and
  settings. After the window the miner reconnects with your settings and carries on. Each switch costs a reconnect and a job
  preparation (about 1-2 seconds measured on an RTX 5080).
- The developer's account is compiled in (`src/product/fee.h`); a build compiled without it never donates, and
  `canary-miner version` says `donation=compiled-in` or `donation=not compiled in`.
- Your shares and the donation shares are never mixed: the stats endpoint keeps them under `donation.outcomes`, separate
  from your device and total counters. `donation.pool` reports the donation pool host and `donation.login` reads `developer`.
- Only productive time counts: time spent connecting, without a job, or stopped never accrues. Progress is saved in
  `donation-state.json` (in the log directory, or the state directory when no log directory is set); a damaged file is
  ignored with a warning and a fresh cycle starts.
- If the donation pool is unreachable or sends no work for 60 seconds, the window is abandoned, your mining resumes at once,
  and the unmet time is carried as debt (never more than one extra minute). The window is retried after 10 minutes. A
  failure never stops your mining and never counts as a failure of your own pool connection. If you stop the miner during a
  window, the unmet part is carried over to the next start.

Running with the donation off is official and supported; it is still CanaryMiner. See [TRADEMARK.md](TRADEMARK.md) for what a
renamed fork must call itself.

## Troubleshooting

| Problem | What to do |
|---|---|
| "Windows protected your PC" | The binary is not code-signed yet. Check the SHA-256, then **More info** and **Run anyway**. |
| Missing DLL (Windows) | Install "Microsoft Visual C++ Redistributable (x64)". |
| `no usable supported device` | Only RTX 50-series GPUs are supported. Update the driver (CUDA 13.1 support) and run `list-devices`. |
| Low hashrate right after start | Normal; allow about 5 minutes. |
| Pool keeps disconnecting | Check the pool host and port, your wallet or account, and your firewall. On F2Pool, use your account name, not a prl1 address. |
| Rejected or invalid shares | Usually unstable tuning; go back to stock clocks. |
| A GPU keeps restarting | Open the **D** page for its last error; undo recent tuning. |
| `Permission denied` (Linux) | `chmod +x canary-miner start*.sh` |
| `GLIBC_2.34 not found` (Linux) | Your distribution is too old; use Ubuntu 22.04 or newer. |

Report bugs on the [issue tracker](https://github.com/bucnazty2020/CanaryMiner/issues); see [SECURITY.md](SECURITY.md)
for vulnerabilities.

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

## Third-party software

CanaryMiner contains and was built with third-party material under its own licences. See [THIRD_PARTY.md](THIRD_PARTY.md).

## Documentation

- [CONTRIBUTING.md](CONTRIBUTING.md): gates, sign-off and the no-hand-SASS rule
- [SECURITY.md](SECURITY.md): how to report a vulnerability
- [TRADEMARK.md](TRADEMARK.md): use of the CanaryMiner name and logo
