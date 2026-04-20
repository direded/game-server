# flatc-compile.ps1 — regenerate C++ headers from ../protocol/*.fbs if any schema
# file is newer than its generated header. Invoked as a premake pre-build step.

$ErrorActionPreference = "Stop"
$SERVER_ROOT   = Split-Path -Parent $PSScriptRoot
$UMBRELLA_ROOT = Split-Path -Parent $SERVER_ROOT
$PROTOCOL_DIR  = Join-Path $UMBRELLA_ROOT "protocol"
$FLATC         = Join-Path $SERVER_ROOT "_tools\flatc.exe"
$OUT_DIR       = Join-Path $SERVER_ROOT "src\protocol\generated"

if (-Not (Test-Path $FLATC)) {
    Write-Error "flatc not found at $FLATC. Run scripts/setup.ps1 first."
    exit 1
}

if (-Not (Test-Path $PROTOCOL_DIR)) {
    Write-Error "protocol/ not found at $PROTOCOL_DIR. Did 'git submodule update --init' run?"
    exit 1
}

New-Item -ItemType Directory -Force -Path $OUT_DIR | Out-Null

$fbs_files = @(Get-ChildItem -Path $PROTOCOL_DIR -Filter "*.fbs" -File)
if ($fbs_files.Count -eq 0) {
    Write-Host "flatc-compile: no .fbs files in $PROTOCOL_DIR; nothing to do."
    exit 0
}

# Incremental: regenerate only if any schema is newer than its generated header
# (or if any included schema changed — simplest correct rule: if *any* .fbs is
# newer than *any* generated header, regenerate everything).
$needs_regen = $false
$newest_fbs = ($fbs_files | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime
foreach ($fbs in $fbs_files) {
    $base      = [IO.Path]::GetFileNameWithoutExtension($fbs.Name)
    $generated = Join-Path $OUT_DIR ($base + "_generated.h")
    if (-Not (Test-Path $generated)) {
        $needs_regen = $true
        break
    }
    if ($newest_fbs -gt (Get-Item $generated).LastWriteTime) {
        $needs_regen = $true
        break
    }
}

if (-Not $needs_regen) {
    Write-Host "flatc-compile: generated headers up-to-date ($($fbs_files.Count) schema(s))."
    exit 0
}

Write-Host "flatc-compile: regenerating $($fbs_files.Count) schema(s) -> $OUT_DIR"
$paths = $fbs_files | ForEach-Object { $_.FullName }
& $FLATC --cpp -o $OUT_DIR @paths
if ($LASTEXITCODE -ne 0) {
    Write-Error "flatc failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}
Write-Host "flatc-compile: done."
