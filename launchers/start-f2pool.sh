#!/bin/sh
# CanaryMiner on F2Pool. Edit YOUR_F2POOL_ACCOUNT to your F2Pool account name, then run ./start-f2pool.sh.
# F2Pool logs in with your account name, not a prl1 address. Set your PRL payout address in your F2Pool account settings.
cd "$(dirname "$0")" || exit 1
exec ./canary-miner --pool f2pool --wallet YOUR_F2POOL_ACCOUNT.rig1 "$@"
