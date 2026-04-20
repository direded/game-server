# run-tests.ps1 - run game-server-tests against a Postgres test database.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts/run-tests.ps1 [options]
#
# Options:
#   -Mode <local|docker>      Database to test against. Default: local.
#   -Config <Debug|Release>   Build config of the test binary. Default: Debug.
#   -Filter <gtest filter>    Optional --gtest_filter to forward.
#   -DockerPort <port>        Host port to bind the docker container to. Default: 5433.
#   -DockerImage <image>      Postgres image. Default: postgres:18-alpine.
#
# Modes:
#   local   - assumes a long-lived `game_test` database on localhost:5432 with
#             migrations applied. Fast (no startup overhead) but state persists
#             across runs. Test fixtures TRUNCATE the relevant tables on each
#             test, so cross-test leakage is bounded; cross-run leakage of
#             schema-level state (extensions, ALTERs, etc.) is not.
#             Override the URL via $env:TEST_DATABASE_URL_LOCAL if your local
#             DB lives somewhere else.
#
#   docker  - spins up an ephemeral Postgres container, applies all migrations
#             via dbmate, runs the test binary, and tears the container down
#             afterwards (even on test failure). Slower (~3-5 s startup) but
#             every run starts from a known-clean schema. Requires Docker.
#
# Exit code is the test binary's exit code (or 1 on infrastructure failure).

[CmdletBinding()]
param(
    [ValidateSet('local','docker')]
    [string] $Mode = 'local',

    [ValidateSet('Debug','Release')]
    [string] $Config = 'Debug',

    [string] $Filter = '',
    [int]    $DockerPort = 5433,
    [string] $DockerImage = 'postgres:18-alpine'
)

$ErrorActionPreference = 'Stop'
$ROOT = Split-Path -Parent (Split-Path -Parent $PSCommandPath)

$tests = Join-Path $ROOT "bin/$Config/game-server-tests.exe"
if (-not (Test-Path $tests)) {
    Write-Host "Test binary not found at $tests" -ForegroundColor Red
    Write-Host "Build first: msbuild build/game-server.sln -p:Configuration=$Config" -ForegroundColor Yellow
    exit 1
}

function Invoke-Tests([string] $url) {
    # Note: don't capture / return the test binary's stdout here - that would
    # swallow GoogleTest's output. The caller reads $LASTEXITCODE directly
    # after this function returns (it's a global).
    $env:TEST_DATABASE_URL = $url
    $argv = @()
    if ($Filter) { $argv += "--gtest_filter=$Filter" }
    & $tests @argv
}

# --- local mode -----------------------------------------------------------
if ($Mode -eq 'local') {
    $url = if ($env:TEST_DATABASE_URL_LOCAL) {
        $env:TEST_DATABASE_URL_LOCAL
    } else {
        'postgres://postgres:root@localhost:5432/game_test?sslmode=disable'
    }
    Write-Host "Mode: local - $url" -ForegroundColor Cyan
    Invoke-Tests $url
    exit $LASTEXITCODE
}

# --- docker mode ----------------------------------------------------------
if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    Write-Host "Docker CLI not found. Install Docker Desktop or use -Mode local." -ForegroundColor Red
    exit 1
}

$dbmate = Join-Path $ROOT '_tools/dbmate.exe'
if (-not (Test-Path $dbmate)) {
    Write-Host "dbmate not found at $dbmate. Run scripts/setup.ps1 first." -ForegroundColor Red
    exit 1
}

$container = "game-test-pg-$(Get-Random)"
$pgPass = 'test'
$portSpec = "$($DockerPort):5432"

Write-Host "Mode: docker - starting $DockerImage as $container on host port $DockerPort..." -ForegroundColor Cyan
& docker run -d --rm --name $container `
    -e "POSTGRES_PASSWORD=$pgPass" `
    -e "POSTGRES_DB=game_test" `
    -p $portSpec `
    $DockerImage | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "docker run failed" -ForegroundColor Red
    exit 1
}

$rc = 1
try {
    # Wait for the server to accept connections (pg_isready inside the container).
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline) {
        & docker exec $container pg_isready -U postgres 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) { break }
        Start-Sleep -Milliseconds 250
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Postgres in container did not become ready within 30s"
    }

    $url = "postgres://postgres:$pgPass@localhost:$DockerPort/game_test?sslmode=disable"

    Write-Host "Applying migrations..." -ForegroundColor Cyan
    Push-Location $ROOT
    try {
        $env:DATABASE_URL = $url
        & $dbmate up
        if ($LASTEXITCODE -ne 0) { throw "dbmate up failed" }
    } finally { Pop-Location }

    Write-Host "Running tests..." -ForegroundColor Cyan
    Invoke-Tests $url
    $rc = $LASTEXITCODE
} catch {
    Write-Host $_ -ForegroundColor Red
    $rc = 1
} finally {
    Write-Host "Stopping container $container..." -ForegroundColor DarkGray
    & docker stop $container 2>&1 | Out-Null
}
exit $rc
