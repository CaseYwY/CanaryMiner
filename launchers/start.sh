#!/bin/sh
# CanaryMiner, basic launcher. Edit YOUR_PRL_ADDRESS to your public PRL payout address, then run ./start.sh.
# ".rig1" names this rig on the pool; change it or leave it.
cd "$(dirname "$0")" || exit 1
exec ./canary-miner --pool stratum+tcp://pearl.f2pool.com:5500 --wallet YOUR_PRL_ADDRESS.rig1 "$@"
