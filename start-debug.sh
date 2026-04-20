#!/usr/bin/env bash
# start-debug.sh — launch the Debug build with config.yaml.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
exec "$ROOT/bin/Debug/game-server" "$ROOT/config.yaml"
