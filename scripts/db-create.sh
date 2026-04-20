#!/usr/bin/env bash
# db-create.sh — create the database named in .env's DATABASE_URL using psql.
# Usage:
#   scripts/db-create.sh
#
# Idempotent: exits 0 if the database already exists. Uses `psql` from PATH
# (install postgresql-client). Admin credentials come from DATABASE_URL; use
# a role with CREATEDB privilege (e.g. the default "postgres" role).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENV_FILE="$ROOT/.env"

if [[ ! -f "$ENV_FILE" ]]; then
    echo ".env not found. Copy .env.example to .env and edit credentials." >&2
    exit 1
fi

if ! command -v psql >/dev/null 2>&1; then
    echo "psql not found. Install postgresql-client (Debian/Ubuntu: sudo apt install postgresql-client)." >&2
    exit 1
fi

# Parse DATABASE_URL from .env (simple KEY=VALUE; ignores comments/blanks).
db_url=""
while IFS= read -r line; do
    if [[ "$line" =~ ^[[:space:]]*DATABASE_URL[[:space:]]*=[[:space:]]*(.+)[[:space:]]*$ ]]; then
        db_url="${BASH_REMATCH[1]}"
        # strip surrounding quotes
        db_url="${db_url%\"}"; db_url="${db_url#\"}"
        db_url="${db_url%\'}"; db_url="${db_url#\'}"
        break
    fi
done < "$ENV_FILE"

if [[ -z "$db_url" ]]; then
    echo "DATABASE_URL not found in .env" >&2
    exit 1
fi

# postgres://user:pass@host:port/dbname?params
re='^postgres(ql)?://(([^:@]+)(:([^@]*))?@)?([^:/?]+)(:([0-9]+))?/([^?]+)'
if ! [[ "$db_url" =~ $re ]]; then
    echo "Failed to parse DATABASE_URL: $db_url" >&2
    exit 1
fi
user="${BASH_REMATCH[3]:-postgres}"
pass="${BASH_REMATCH[5]:-}"
host="${BASH_REMATCH[6]}"
port="${BASH_REMATCH[8]:-5432}"
db="${BASH_REMATCH[9]}"

echo "Target: database '$db' on $host:$port as $user"

if [[ -n "$pass" ]]; then
    export PGPASSWORD="$pass"
fi

exists=$(psql -h "$host" -p "$port" -U "$user" -d postgres -tAc \
    "SELECT 1 FROM pg_database WHERE datname = '$db'") || {
    echo "psql connection failed. Check that Postgres is running and credentials are correct." >&2
    exit 1
}

if [[ "$exists" == "1" ]]; then
    echo "Database '$db' already exists — nothing to do."
    exit 0
fi

psql -h "$host" -p "$port" -U "$user" -d postgres -c "CREATE DATABASE \"$db\""
echo "Database '$db' created."
echo "Next: scripts/db-migrate.sh up"
