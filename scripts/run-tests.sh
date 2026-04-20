#!/usr/bin/env bash
# run-tests.sh — Linux equivalent of run-tests.ps1.
#
# Usage:
#   scripts/run-tests.sh [--mode local|docker] [--config Debug|Release]
#                        [--filter <gtest filter>] [--port <docker port>]
#                        [--image <postgres image>]
#
# Modes mirror the PowerShell script:
#   local   — long-lived game_test on localhost:5432 (fast iteration).
#             Override URL via $TEST_DATABASE_URL_LOCAL.
#   docker  — ephemeral postgres container, fresh schema every run.

set -euo pipefail

MODE="local"
CONFIG="Debug"
FILTER=""
DOCKER_PORT="5433"
DOCKER_IMAGE="postgres:18-alpine"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --mode|-m)        MODE="$2"; shift 2 ;;
        --config|-c)      CONFIG="$2"; shift 2 ;;
        --filter|-f)      FILTER="$2"; shift 2 ;;
        --port|-p)        DOCKER_PORT="$2"; shift 2 ;;
        --image|-i)       DOCKER_IMAGE="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,12p' "$0"; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; exit 2 ;;
    esac
done

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TESTS="$ROOT/bin/$CONFIG/game-server-tests"

if [[ ! -x "$TESTS" ]]; then
    echo "Test binary not found at $TESTS" >&2
    echo "Build first: make -C build config=$(echo "$CONFIG" | tr '[:upper:]' '[:lower:]')_x64 -j\$(nproc)" >&2
    exit 1
fi

run_tests() {
    local url="$1"
    export TEST_DATABASE_URL="$url"
    if [[ -n "$FILTER" ]]; then
        "$TESTS" --gtest_filter="$FILTER"
    else
        "$TESTS"
    fi
}

# --- local mode -----------------------------------------------------------
if [[ "$MODE" == "local" ]]; then
    URL="${TEST_DATABASE_URL_LOCAL:-postgres://postgres:root@localhost:5432/game_test?sslmode=disable}"
    echo "Mode: local — $URL"
    run_tests "$URL"
    exit $?
fi

# --- docker mode ----------------------------------------------------------
if [[ "$MODE" != "docker" ]]; then
    echo "Unknown mode: $MODE (use local or docker)" >&2
    exit 2
fi

if ! command -v docker >/dev/null 2>&1; then
    echo "Docker CLI not found. Install docker or use --mode local." >&2
    exit 1
fi

DBMATE="$ROOT/_tools/dbmate"
if [[ ! -x "$DBMATE" ]]; then
    echo "dbmate not found at $DBMATE. Run scripts/setup.sh first." >&2
    exit 1
fi

CONTAINER="game-test-pg-$RANDOM"
PG_PASS="test"

echo "Mode: docker — starting $DOCKER_IMAGE as $CONTAINER on host port $DOCKER_PORT..."
docker run -d --rm --name "$CONTAINER" \
    -e POSTGRES_PASSWORD="$PG_PASS" \
    -e POSTGRES_DB="game_test" \
    -p "${DOCKER_PORT}:5432" \
    "$DOCKER_IMAGE" >/dev/null

# Always tear the container down, even if anything below fails.
cleanup() {
    echo "Stopping container $CONTAINER..."
    docker stop "$CONTAINER" >/dev/null 2>&1 || true
}
trap cleanup EXIT

# Poll an authenticated SELECT inside the container — pg_isready returns ready
# during initdb's no-auth phase, before POSTGRES_PASSWORD takes effect.
deadline=$(( $(date +%s) + 60 ))
ready=0
while (( $(date +%s) < deadline )); do
    if docker exec -e PGPASSWORD="$PG_PASS" "$CONTAINER" \
        psql -h 127.0.0.1 -U postgres -d game_test -tAc "SELECT 1" >/dev/null 2>&1
    then
        ready=1; break
    fi
    sleep 0.5
done
if (( ready == 0 )); then
    echo "Postgres in container did not accept authenticated connections within 60s" >&2
    exit 1
fi

URL="postgres://postgres:$PG_PASS@localhost:$DOCKER_PORT/game_test?sslmode=disable"

echo "Applying migrations..."
( cd "$ROOT" && DATABASE_URL="$URL" "$DBMATE" up )

echo "Running tests..."
run_tests "$URL"
