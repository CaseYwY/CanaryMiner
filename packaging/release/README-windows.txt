CanaryMiner @VERSION@ - Windows quick start
============================================

CanaryMiner mines Pearl (PRL) on NVIDIA RTX 50-series GPUs (Blackwell). It works with any Pearl pool that speaks the
Pearl pool protocol; it is tested on F2Pool and Kryptex.

Requirements
- Windows 10 or 11, 64-bit.
- NVIDIA RTX 50-series GPU (Blackwell, SM120). This release does not run on other GPUs.
- An NVIDIA driver that supports CUDA 13.1 (tested with 616.92 or newer). The CUDA toolkit is not needed.
- The Microsoft Visual C++ 2015-2022 x64 runtime. If canary-miner.exe reports a missing DLL, install
  "Microsoft Visual C++ Redistributable (x64)" from Microsoft.
- The binary is not code-signed yet, so Windows may show "Windows protected your PC". Check the download (see the end
  of this file), then click "More info" and "Run anyway".

1. Set your address
   Right-click start.bat, choose Edit (or open it in Notepad). Replace YOUR_PRL_ADDRESS with your public PRL payout address,
   which starts with prl1. Use a public payout address only. Never put wallet secrets or passwords in these files.
   The ".rig1" part names this rig on the pool. Change it if you run more than one rig.

2. Start
   Double-click start.bat. The window shows the miner's output. Stop with Ctrl+C, or close the window.

Launchers in this folder
   start.bat            the basic launcher (Kryptex), two lines to edit
   start-f2pool.bat     F2Pool, pre-filled: put your F2Pool account name, not a prl1 address
   start-kryptex.bat    Kryptex, pre-filled
   start-herominers.bat HeroMiners, pre-filled
   start-advanced.bat   editable settings (wallet, worker, pool, donation, extra options) and a restart after the
                        miner stops; press Ctrl+C and then Y to quit
   Any option works on the command line too, for example:
                        canary-miner.exe --pool kryptex --wallet YOUR_PRL_ADDRESS.rig1 --gpu-id 0

Pools
   --pool f2pool | kryptex | herominers   the known pools (the preset sets the pool's rules)
   --pool HOST:PORT                       any other Pearl pool
   --pool stratum+tcp://HOST:PORT         the same, with the scheme spelled out
   stratum+ssl:// (TLS) is not supported yet.
   F2Pool logs in with your F2Pool account name, not a prl1 address: --wallet YOUR_F2POOL_ACCOUNT.rig1
   (set your PRL payout address in your F2Pool account settings).

Options (canary-miner.exe --help lists them all)
   -o, --pool SPEC        pool, as above
   -u, --wallet ADDR      your PRL address; ADDR.WORKER also sets the worker
       --worker NAME      rig name (overrides the part after the dot)
   -p, --password P       pool password field (default x)
       --gpu-id LIST      GPUs to use: all, or indexes such as 0,1
       --api-enable       turn the stats endpoint on at 127.0.0.1:21550
       --api-port N       stats endpoint port (0 turns it off)
       --no-donation      turn the developer donation off
       --config FILE      config file (copy config.example.json); command-line options win
   GPU clocks, memory, power limit and fan are set with MSI Afterburner (or a similar tool), not by
   CanaryMiner. Apply your profile before starting the miner; CanaryMiner mines with whatever is applied.

Developer donation
- On by default: 1% of productive mining time is mined to the developer on F2Pool, in one-minute windows inside each
  100 productive minutes. The stats endpoint reports it separately from your own shares.
- Turn it off with --no-donation, or set "donation": false in a config file. The command-line flag wins.

Stats
- The stats endpoint is on by default at 127.0.0.1:8085 (loopback only): /api/v1/stats and /api/v1/version.
  --api-enable moves it to 127.0.0.1:21550 unless you give --api-port.

Verify this download
  In PowerShell, in the folder with the zip:
     Get-FileHash .\canaryminer-@VERSION@-windows-x86_64.zip -Algorithm SHA256
  The hash must match the SHA256 line published with this release.

Licences: LICENSE.txt (MIT), THIRD_PARTY.txt and LICENSES\.
HELP.txt explains changing pools and wallets, GPU tuning and everyday use.
The full documentation is in the README on the project page: https://github.com/bucnazty2020/CanaryMiner
