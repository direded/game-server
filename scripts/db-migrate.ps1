# db-migrate.ps1 — thin wrapper around dbmate.
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts/db-migrate.ps1 up
#   powershell -ExecutionPolicy Bypass -File scripts/db-migrate.ps1 down
#   powershell -ExecutionPolicy Bypass -File scripts/db-migrate.ps1 new create_players
#   powershell -ExecutionPolicy Bypass -File scripts/db-migrate.ps1 status
#
# Loads DATABASE_URL from server/.env (copy from .env.example first).
# dbmate binary is fetched into _tools/ by scripts/setup.ps1.

$ErrorActionPreference = "Stop"
$ROOT = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$DBMATE = Join-Path $ROOT "_tools\dbmate.exe"
$ENV_FILE = Join-Path $ROOT ".env"

if (-Not (Test-Path $DBMATE)) {
    Write-Host "dbmate not found at $DBMATE" -ForegroundColor Red
    Write-Host "Run scripts/setup.ps1 first." -ForegroundColor Yellow
    exit 1
}

if (-Not (Test-Path $ENV_FILE)) {
    Write-Host ".env not found. Copy .env.example to .env and edit credentials." -ForegroundColor Red
    exit 1
}

Push-Location $ROOT
try {
    # dbmate picks up .env from CWD and migrations from ./db/migrations by default.
    & $DBMATE @args
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
