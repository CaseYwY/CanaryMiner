#!/bin/sh
# Argument pass-through only. Everything else (config, devices, fee, shutdown, stats) is the native binary's job.
exec "$(dirname "$0")/canary-miner" "$@"
