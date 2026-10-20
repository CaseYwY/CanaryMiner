CanaryMiner @VERSION@ - Linux quick start
==========================================

CanaryMiner mines Pearl (PRL) on NVIDIA RTX 50-series GPUs (Blackwell). It works with any Pearl pool that speaks the
Pearl pool protocol; it is tested on F2Pool and Kryptex.

Requirements
- x86-64 Linux with glibc @GLIBC@ or newer (Ubuntu 22.04, 24.04 and later).
- NVIDIA RTX 50-series GPU (Blackwell, SM120). This release does not run on other GPUs.
- @MIN_DRIVER@ The driver's libcuda.so.1 is the only NVIDIA library this archive needs. The CUDA toolkit is not needed.

1. Set your address
   Open start.sh in a text editor. Replace YOUR_PRL_ADDRESS with your public PRL payout address, which starts with prl1.
   Use a public payout address only. Never put wallet secrets or passwords in these files.
   The ".rig1" part names this rig on the pool. Change it if you run more than one rig.

2. Start
   ./start.sh
   The terminal shows the miner's output. Stop with Ctrl+C.

Launchers in this folder
   start.sh             the basic launcher (F2Pool), two lines to edit
   start-f2pool.sh      F2Pool, pre-filled
   start-kryptex.sh     Kryptex, pre-filled
   start-herominers.sh  HeroMiners, pre-filled
   start-advanced.sh    editable settings (wallet, worker, pool, donation, extra options) and a restart after the
                        miner stops; set RESTART=0 to run it once
   Any option works on the command line too, for example:
                        ./canary-miner --pool f2pool --wallet YOUR_PRL_ADDRESS.rig1 --gpu-id 0

Pools
   --pool f2pool | kryptex | herominers   the known pools (the preset sets the pool's rules)
   --pool HOST:PORT                       any other Pearl pool
   --pool stratum+tcp://HOST:PORT         the same, with the scheme spelled out
   stratum+ssl:// (TLS) is not supported yet.
   F2Pool account users: --wallet YOUR_ACCOUNT.rig1 uses your F2Pool account instead of a prl1 address.

Options (./canary-miner --help lists them all)
   -o, --pool SPEC        pool, as above
   -u, --wallet ADDR      your PRL address; ADDR.WORKER also sets the worker
       --worker NAME      rig name (overrides the part after the dot)
   -p, --password P       pool password field (default x)
       --gpu-id LIST      GPUs to use: all, or indexes such as 0,1
       --api-enable       turn the stats endpoint on at 127.0.0.1:21550
       --api-port N       stats endpoint port (0 turns it off)
       --no-donation      turn the developer donation off
       --config FILE      config file (copy config.example.json); command-line options win
   GPU clocks, memory, power limit and fan are set with nvidia-smi (or your mining OS's tool), not by
   CanaryMiner. Apply them before starting the miner; HELP.txt shows the commands.

Developer donation
- On by default: 1% of productive mining time is mined to the developer on F2Pool, in one-minute windows inside each
  100 productive minutes. The stats endpoint reports it separately from your own shares.
- Turn it off with --no-donation, or set "donation": false in a config file. The command-line flag wins.

Stats
- The stats endpoint is on by default at 127.0.0.1:8085 (loopback only): /api/v1/stats and /api/v1/version.
  --api-enable moves it to 127.0.0.1:21550 unless you give --api-port.

Verify this download
  sha256sum -c SHA256SUMS

Licences: LICENSE (MIT), THIRD_PARTY.md and LICENSES/.
HELP.txt explains changing pools and wallets, GPU tuning and everyday use.
The full documentation is in the README on the project page: https://github.com/bucnazty2020/CanaryMiner
