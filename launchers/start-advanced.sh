#!/bin/sh
# CanaryMiner, editable launcher. Edit the settings below, then run ./start-advanced.sh.
# The miner restarts itself 5 seconds after it stops (Ctrl+C quits). Set RESTART=0 to run it once.

# ---- settings: edit these ----
# Your public PRL payout address. The launcher refuses to start until you change it.
WALLET="PUT_YOUR_PRL_ADDRESS_HERE"
# Rig name shown by the pool.
WORKER="rig1"
# Pool: kryptex, herominers, f2pool, or your own HOST:PORT / stratum+tcp://HOST:PORT.
# For f2pool, set WALLET to your F2Pool account name (your PRL address is set in your F2Pool account).
POOL="kryptex"
# Developer donation: on (1%) or off.
DONATION="on"
# Extra options, for example "--gpu-id 0" or "--api-enable". Leave empty for none.
EXTRA=""
# 1 restarts the miner after it stops; 0 runs it once.
RESTART=1
# ---- end of settings ----

cd "$(dirname "$0")" || exit 1
if [ ! -x ./canary-miner ]; then
  echo "canary-miner was not found next to this launcher. Extract the whole archive and try again." >&2
  exit 1
fi
if [ "$WALLET" = "PUT_YOUR_PRL_ADDRESS_HERE" ]; then
  echo "Edit WALLET in start-advanced.sh to your PRL address, then run it again." >&2
  exit 1
fi

donation_flag=""
if [ "$DONATION" = "off" ]; then donation_flag="--no-donation"; fi
echo "CanaryMiner: pool $POOL, worker $WORKER, donation $DONATION."

while :; do
  # $EXTRA is deliberately unquoted so that several options split into words.
  # shellcheck disable=SC2086
  ./canary-miner --pool "$POOL" --wallet "$WALLET" --worker "$WORKER" $donation_flag $EXTRA
  status=$?
  if [ "$RESTART" != "1" ]; then exit "$status"; fi
  echo "The miner stopped (exit code $status). It restarts in 5 seconds. Press Ctrl+C to quit."
  sleep 5
done
