#!/bin/sh
# CanaryMiner, editable launcher. Edit the settings below, then run ./start-advanced.sh.
# Only retryable runtime errors restart, at most MAX_RESTARTS times. Q/Ctrl+C/stop and latched faults never restart.

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
# 1 retries exit code 1 only; 0 runs it once. Clean stops, signals and exhausted worker budgets never retry.
RESTART=1
MAX_RESTARTS=3
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

child_pid=""
stop_launcher() {
  trap '' INT TERM HUP
  if [ -n "$child_pid" ]; then
    kill -TERM "$child_pid" 2>/dev/null
    wait "$child_pid" 2>/dev/null
  fi
  exit "$1"
}
trap 'stop_launcher 130' INT
trap 'stop_launcher 143' TERM
trap 'stop_launcher 129' HUP
case "$MAX_RESTARTS" in [0-9]|[0-9][0-9]) ;; *) echo "MAX_RESTARTS must be an integer from 0 to 99." >&2; exit 2;; esac
restarts=0

while :; do
  # $EXTRA is deliberately unquoted so that several options split into words.
  # shellcheck disable=SC2086
  ./canary-miner --pool "$POOL" --wallet "$WALLET" --worker "$WORKER" $donation_flag $EXTRA <&0 &
  child_pid=$!
  wait "$child_pid"
  status=$?
  child_pid=""
  if [ "$RESTART" != "1" ]; then exit "$status"; fi
  if [ "$status" != "1" ] || [ "$restarts" -ge "$MAX_RESTARTS" ]; then exit "$status"; fi
  restarts=$((restarts + 1))
  echo "Retryable runtime error; restart $restarts/$MAX_RESTARTS in 5 seconds. Press Ctrl+C to quit."
  sleep 5 &
  child_pid=$!
  wait "$child_pid"
  child_pid=""
done
