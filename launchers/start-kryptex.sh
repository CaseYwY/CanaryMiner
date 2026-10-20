#!/bin/sh
# CanaryMiner on Kryptex. Edit YOUR_PRL_ADDRESS to your public PRL payout address, then run ./start-kryptex.sh.
cd "$(dirname "$0")" || exit 1
exec ./canary-miner --pool kryptex --wallet YOUR_PRL_ADDRESS.rig1 "$@"
