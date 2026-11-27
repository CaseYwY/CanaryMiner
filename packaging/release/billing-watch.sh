#!/usr/bin/env bash
# Print a systemd user service for the miner installed alongside this directory.
set -euo pipefail

usage() {
    printf '%s\n' 'Usage: billing-watch.sh SEED.cmr SECONDS [--confirm-electricity-import] [--allow-public-rates]' >&2
    printf '%s\n' '       billing-watch.sh SEED.cmr SECONDS --account-capture /absolute/path/to/python3' >&2
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
python=
capture=
options=()
while (( $# )); do
    option=$1
    shift
    case $option in
        --confirm-electricity-import|--allow-public-rates) ;;
        --account-capture)
            [[ -z $python && $# -eq 1 && ${#options[@]} -eq 0 && $1 = /* && -x $1 ]] || usage
            IFS= read -r -d '' python < <(realpath -ze -- "$1") || usage
            shift
            continue
            ;;
        *) usage ;;
    esac
    for previous in "${options[@]}"; do
        [[ $previous != "$option" ]] || usage
    done
    options+=("$option")
done
if [[ -n $python ]]; then
    credential=${seed%/*}/.canary-octopus-capture.cred
    capture=${deploy%/*}/tools/capture_octopus_consumption.py
    (( seconds >= 60 )) && [[ -f $python && -f $credential && ! -L $credential && -f $capture && ! -L $capture ]] || usage
fi

quote_arg() {
    local value=$1
    value=${value//\\/\\\\}
    value=${value//\"/\\\"}
    value=${value//%/%%}
    printf '"%s"' "$value"
}

for path in "$seed" "$miner" "$python" "$capture"; do
    [[ $path != *[$'\r\n']* ]] || usage
done

printf '[Unit]\nDescription='
if [[ -n $python ]]; then
    printf '%s\n' 'CanaryMiner scheduled Octopus import readings'
else
    printf '%s\n' 'CanaryMiner recurring Octopus billing review'
fi
cat <<'UNIT'

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
printf 'ExecStart=:/bin/sh -c %s canaryminer %s ' \
    "$(quote_arg 'cd "$1" && shift && exec "$@"')" "$(quote_arg "${seed%/*}/")"
if [[ -n $python ]]; then
    printf '%s -I -B %s --confirm-electricity-import --run-scheduled %s --watch-every-seconds %s' \
        "$(quote_arg "$python")" "$(quote_arg "$capture")" "$(quote_arg "$seed")" "$seconds"
else
    printf '%s meter-bill --roll-forward %s --watch-every-seconds %s' \
        "$(quote_arg "$miner")" "$(quote_arg "$seed")" "$seconds"
    for option in "${options[@]}"; do
        printf ' %s' "$option"
    done
fi
printf '\n\n[Install]\nWantedBy=default.target\n'
