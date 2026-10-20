#!/bin/sh
# CanaryMiner on HeroMiners. Edit YOUR_PRL_ADDRESS to your public PRL payout address, then run ./start-herominers.sh.
cd "$(dirname "$0")" || exit 1
exec ./canary-miner --pool herominers --wallet YOUR_PRL_ADDRESS.rig1 "$@"
