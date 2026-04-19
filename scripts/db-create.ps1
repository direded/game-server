# db-create.ps1 — create the database named in .env's DATABASE_URL using psql.
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts/db-create.ps1
#
# Idempotent: exits 0 if the database already exists.
# Uses psql from the detected PostgreSQL install (_tools/pg_root.txt),
# falling back to psql on PATH. Admin credentials come from DATABASE_URL;
# use a role with CREATEDB privilege (e.g. the default "postgres" role).

$ErrorActionPreference = "Stop"
$ROOT = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$ENV_FILE = Join-Path $ROOT ".env"
$PG_ROOT_FILE = Join-Path $ROOT "_tools\pg_root.txt"

if (-Not (Test-Path $ENV_FILE)) {
    Write-Host ".env not found. Copy .env.example to .env and edit credentials." -ForegroundColor Red
    exit 1
}

# Resolve psql: prefer the detected Postgres install so the script works
# even if PostgreSQL's bin dir isn't on PATH.
$PSQL = "psql"
if (Test-Path $PG_ROOT_FILE) {
    $candidate = Join-Path ((Get-Content $PG_ROOT_FILE -Raw).Trim()) "bin\psql.exe"
    if (Test-Path $candidate) { $PSQL = $candidate }
}

# Parse DATABASE_URL from .env (simple KEY=VALUE lines; ignores comments/blanks).
$dbUrl = $null
foreach ($line in Get-Content $ENV_FILE) {
    if ($line -match '^\s*DATABASE_URL\s*=\s*(.+?)\s*$') {
        $dbUrl = $matches[1].Trim('"').Trim("'")
        break
    }
}
if (-Not $dbUrl) {
    Write-Host "DATABASE_URL not found in .env" -ForegroundColor Red
    exit 1
}

# postgres://user:pass@host:port/dbname?params
$pattern = '^postgres(?:ql)?://(?:(?<user>[^:@]+)(?::(?<pass>[^@]*))?@)?(?<host>[^:/?]+)(?::(?<port>\d+))?/(?<db>[^?]+)'
if ($dbUrl -notmatch $pattern) {
    Write-Host "Failed to parse DATABASE_URL: $dbUrl" -ForegroundColor Red
    exit 1
}
$user   = if ($Matches['user']) { $Matches['user'] } else { 'postgres' }
$pgPass = $Matches['pass']
$pgHost = $Matches['host']
$port   = if ($Matches['port']) { $Matches['port'] } else { '5432' }
$db     = $Matches['db']

Write-Host "Target: database '$db' on $pgHost`:$port as $user" -ForegroundColor Cyan

if ($pgPass) { $env:PGPASSWORD = $pgPass }

# Check existence by querying pg_database on the admin db.
$exists = & $PSQL -h $pgHost -p $port -U $user -d postgres -tAc "SELECT 1 FROM pg_database WHERE datname = '$db'"
if ($LASTEXITCODE -ne 0) {
    Write-Host "psql connection failed. Check that Postgres is running and credentials are correct." -ForegroundColor Red
    exit $LASTEXITCODE
}
if ($exists -eq '1') {
    Write-Host "Database '$db' already exists — nothing to do." -ForegroundColor Green
    exit 0
}

$sql = 'CREATE DATABASE "' + $db + '"'
& $PSQL -h $pgHost -p $port -U $user -d postgres -c $sql
if ($LASTEXITCODE -ne 0) {
    Write-Host "Failed to create database." -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "Database '$db' created." -ForegroundColor Green
Write-Host "Next: scripts/db-migrate.ps1 up" -ForegroundColor White
