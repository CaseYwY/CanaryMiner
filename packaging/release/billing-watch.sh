#!/usr/bin/env bash
# Print a systemd user service for the miner installed alongside this directory.
set -euo pipefail

usage() {
    printf '%s\n' 'Usage: billing-watch.sh SEED.cmr SECONDS [--confirm-electricity-import] [--allow-public-rates]' >&2
    exit 2
}

(( $# >= 2 )) || usage
[[ $2 =~ ^[1-9][0-9]{0,4}$ ]] && (( $2 <= 86400 )) || usage
seconds=$2
[[ -f $1 && ! -L $1 && -r $1 ]] || usage
IFS= read -r -d '' seed < <(realpath -ze -- "$1") || usage
IFS= read -r -d '' script < <(realpath -ze -- "${BASH_SOURCE[0]}") || usage
deploy=${script%/*}
miner=${deploy%/*}/bin/canary-miner
[[ -f $miner && ! -L $miner && -x $miner ]] || {
    printf '%s\n' 'The installed miner is missing beside deploy/.' >&2
    exit 2
}
shift 2
options=()
for option; do
    case $option in
        --confirm-electricity-import|--allow-public-rates) ;;
        *) usage ;;
    esac
    for previous in "${options[@]}"; do
        [[ $previous != "$option" ]] || usage
    done
    options+=("$option")
done

quote_arg() {
    local value=$1
    value=${value//\\/\\\\}
    value=${value//\"/\\\"}
    value=${value//%/%%}
    printf '"%s"' "$value"
}

for path in "$seed" "$miner"; do
    [[ $path != *[$'\r\n']* ]] || usage
done

cat <<'UNIT'
[Unit]
Description=CanaryMiner recurring Octopus billing review

[Service]
Type=exec
Restart=no
TimeoutStopSec=30s
SuccessExitStatus=130
UMask=0077
StandardOutput=journal
StandardError=journal
UNIT

# Pass paths as data to a fixed command. exec keeps the miner in the same
# process. The systemd colon prefix prevents dollar expansion in the arguments.
printf 'ExecStart=:/bin/sh -c %s canaryminer %s %s meter-bill --roll-forward %s --watch-every-seconds %s' \
    "$(quote_arg 'cd "$1" && shift && exec "$@"')" \
    "$(quote_arg "${seed%/*}/")" "$(quote_arg "$miner")" "$(quote_arg "$seed")" "$seconds"
for option in "${options[@]}"; do
    printf ' %s' "$option"
done
printf '\n\n[Install]\nWantedBy=default.target\n'
