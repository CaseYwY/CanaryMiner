#!/bin/sh
# CanaryMiner on F2Pool. Edit YOUR_PRL_ADDRESS to your public PRL payout address, then run ./start-f2pool.sh.
# F2Pool account users: replace the --wallet value with YOUR_ACCOUNT.rig1 (your F2Pool account, not a prl1 address).
cd "$(dirname "$0")" || exit 1
exec ./canary-miner --pool f2pool --wallet YOUR_PRL_ADDRESS.rig1 "$@"
