# setup.ps1 - Download and set up all vendored dependencies + premake5
# Run from the project root: powershell -ExecutionPolicy Bypass -File scripts/setup.ps1

$ErrorActionPreference = "Stop"
$ROOT = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$VENDOR = Join-Path $ROOT "vendor"
$PREMAKE_DIR = Join-Path $ROOT "_premake5\bin\windows"
$TOOLS_DIR = Join-Path $ROOT "_tools"

Write-Host "=== Game Server - Dependency Setup ===" -ForegroundColor Cyan
Write-Host "Vendor directory: $VENDOR"
Write-Host "Premake directory: $PREMAKE_DIR"
Write-Host "Tools directory:  $TOOLS_DIR"

New-Item -ItemType Directory -Force -Path $VENDOR | Out-Null
New-Item -ItemType Directory -Force -Path $PREMAKE_DIR | Out-Null
New-Item -ItemType Directory -Force -Path $TOOLS_DIR | Out-Null

# ────────────────────────────────────────────────────────────
# 1. Premake5 (build tool)
# ────────────────────────────────────────────────────────────
$PREMAKE_VERSION = "5.0.0-beta5"
$PREMAKE_EXE = Join-Path $PREMAKE_DIR "premake5.exe"

if (-Not (Test-Path $PREMAKE_EXE)) {
    Write-Host "`n[1/6] Downloading Premake5 v$PREMAKE_VERSION..." -ForegroundColor Yellow
    $premakeUrl = "https://github.com/premake/premake-core/releases/download/v$PREMAKE_VERSION/premake-$PREMAKE_VERSION-windows.zip"
    $premakeZip = Join-Path $PREMAKE_DIR "premake.zip"

    Invoke-WebRequest -Uri $premakeUrl -OutFile $premakeZip -UseBasicParsing
    Expand-Archive -Path $premakeZip -DestinationPath $PREMAKE_DIR -Force
    Remove-Item $premakeZip

    Write-Host "  Premake5 ready" -ForegroundColor Green
} else {
    Write-Host "`n[1/6] Premake5 already present" -ForegroundColor Green
}

# ────────────────────────────────────────────────────────────
# 2. Quill (header-only logging library)
# ────────────────────────────────────────────────────────────
$QUILL_VERSION = "11.1.0"
$QUILL_DIR = Join-Path $VENDOR "quill"

if (-Not (Test-Path (Join-Path $QUILL_DIR "include\quill\Backend.h"))) {
    Write-Host "`n[2/6] Downloading Quill v$QUILL_VERSION..." -ForegroundColor Yellow
    $quillUrl = "https://github.com/odygrd/quill/archive/refs/tags/v$QUILL_VERSION.zip"
    $quillZip = Join-Path $VENDOR "quill.zip"

    Invoke-WebRequest -Uri $quillUrl -OutFile $quillZip -UseBasicParsing
    Expand-Archive -Path $quillZip -DestinationPath $VENDOR -Force

    if (Test-Path $QUILL_DIR) { Remove-Item -Recurse -Force $QUILL_DIR }
    Rename-Item -Path (Join-Path $VENDOR "quill-$QUILL_VERSION") -NewName "quill"
    Remove-Item $quillZip

    Write-Host "  Quill ready" -ForegroundColor Green
} else {
    Write-Host "`n[2/6] Quill already present" -ForegroundColor Green
}

# ────────────────────────────────────────────────────────────
# 3. yaml-cpp (compiled as StaticLib by premake)
# ────────────────────────────────────────────────────────────
$YAMLCPP_VERSION = "0.8.0"
$YAMLCPP_DIR = Join-Path $VENDOR "yaml-cpp"

if (-Not (Test-Path (Join-Path $YAMLCPP_DIR "include\yaml-cpp\yaml.h"))) {
    Write-Host "`n[3/6] Downloading yaml-cpp v$YAMLCPP_VERSION..." -ForegroundColor Yellow
    $yamlUrl = "https://github.com/jbeder/yaml-cpp/archive/refs/tags/$YAMLCPP_VERSION.zip"
    $yamlZip = Join-Path $VENDOR "yaml-cpp.zip"

    Invoke-WebRequest -Uri $yamlUrl -OutFile $yamlZip -UseBasicParsing
    Expand-Archive -Path $yamlZip -DestinationPath $VENDOR -Force

    if (Test-Path $YAMLCPP_DIR) { Remove-Item -Recurse -Force $YAMLCPP_DIR }
    Rename-Item -Path (Join-Path $VENDOR "yaml-cpp-$YAMLCPP_VERSION") -NewName "yaml-cpp"
    Remove-Item $yamlZip

    Write-Host "  yaml-cpp ready" -ForegroundColor Green
} else {
    Write-Host "`n[3/6] yaml-cpp already present" -ForegroundColor Green
}

# ────────────────────────────────────────────────────────────
# 4. Google Test (compiled by premake)
# ────────────────────────────────────────────────────────────
$GTEST_VERSION = "1.15.2"
$GTEST_DIR = Join-Path $VENDOR "googletest"

if (-Not (Test-Path (Join-Path $GTEST_DIR "googletest\include\gtest\gtest.h"))) {
    Write-Host "`n[4/6] Downloading Google Test v$GTEST_VERSION..." -ForegroundColor Yellow
    $gtestUrl = "https://github.com/google/googletest/archive/refs/tags/v$GTEST_VERSION.zip"
    $gtestZip = Join-Path $VENDOR "googletest.zip"

    Invoke-WebRequest -Uri $gtestUrl -OutFile $gtestZip -UseBasicParsing
    Expand-Archive -Path $gtestZip -DestinationPath $VENDOR -Force

    if (Test-Path $GTEST_DIR) { Remove-Item -Recurse -Force $GTEST_DIR }
    Rename-Item -Path (Join-Path $VENDOR "googletest-$GTEST_VERSION") -NewName "googletest"
    Remove-Item $gtestZip

    Write-Host "  Google Test ready" -ForegroundColor Green
} else {
    Write-Host "`n[4/6] Google Test already present" -ForegroundColor Green
}

# ────────────────────────────────────────────────────────────
# 5. dbmate (database migration CLI)
# ────────────────────────────────────────────────────────────
$DBMATE_VERSION = "2.22.0"
$DBMATE_EXE = Join-Path $TOOLS_DIR "dbmate.exe"

if (-Not (Test-Path $DBMATE_EXE)) {
    Write-Host "`n[5/6] Downloading dbmate v$DBMATE_VERSION..." -ForegroundColor Yellow
    $dbmateUrl = "https://github.com/amacneil/dbmate/releases/download/v$DBMATE_VERSION/dbmate-windows-amd64.exe"
    Invoke-WebRequest -Uri $dbmateUrl -OutFile $DBMATE_EXE -UseBasicParsing
    Write-Host "  dbmate ready" -ForegroundColor Green
} else {
    Write-Host "`n[5/6] dbmate already present" -ForegroundColor Green
}

# ────────────────────────────────────────────────────────────
# 6. PostgreSQL install detection (for libpq headers + import lib)
# ────────────────────────────────────────────────────────────
Write-Host "`n[6/6] Detecting local PostgreSQL install..." -ForegroundColor Yellow
$PG_ROOT = $env:PGROOT
if (-Not $PG_ROOT) {
    $candidates = Get-ChildItem "C:\Program Files\PostgreSQL" -Directory -ErrorAction SilentlyContinue |
                  Sort-Object Name -Descending
    if ($candidates) {
        $PG_ROOT = $candidates[0].FullName
    }
}

if (-Not $PG_ROOT -or -Not (Test-Path (Join-Path $PG_ROOT "include\libpq-fe.h"))) {
    Write-Host "  PostgreSQL install not found." -ForegroundColor Red
    Write-Host "  Install PostgreSQL (which ships libpq headers + libpq.lib), then either:" -ForegroundColor Yellow
    Write-Host "    - place it under C:\Program Files\PostgreSQL\<version>, or" -ForegroundColor Yellow
    Write-Host "    - set the PGROOT env var to the install dir." -ForegroundColor Yellow
    exit 1
}
Write-Host "  Found: $PG_ROOT" -ForegroundColor Green

# Persist the path so premake can read it.
$pgRootFile = Join-Path $ROOT "_tools\pg_root.txt"
Set-Content -Path $pgRootFile -Value $PG_ROOT -NoNewline

Write-Host "`n=== All dependencies ready ===" -ForegroundColor Cyan
Write-Host "Next: run '_premake5\premake_win.bat' to generate the VS solution." -ForegroundColor White
