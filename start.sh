#!/usr/bin/env bash
# start.sh — launch the Release build with config.yaml.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
exec "$ROOT/bin/Release/game-server" "$ROOT/config.yaml"
