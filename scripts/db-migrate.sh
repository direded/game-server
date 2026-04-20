#!/usr/bin/env bash
# db-migrate.sh — Linux thin wrapper around dbmate.
# Usage:
#   scripts/db-migrate.sh up
#   scripts/db-migrate.sh down
#   scripts/db-migrate.sh new create_players
#   scripts/db-migrate.sh status
#
# Loads DATABASE_URL from server/.env (copy from .env.example first).
# dbmate binary is fetched into _tools/ by scripts/setup.sh.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DBMATE="$ROOT/_tools/dbmate"
ENV_FILE="$ROOT/.env"

if [[ ! -x "$DBMATE" ]]; then
    echo "dbmate not found at $DBMATE" >&2
    echo "Run scripts/setup.sh first." >&2
    exit 1
fi

if [[ ! -f "$ENV_FILE" ]]; then
    echo ".env not found. Copy .env.example to .env and edit credentials." >&2
    exit 1
fi

cd "$ROOT"
exec "$DBMATE" "$@"
