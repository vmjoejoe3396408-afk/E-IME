#Requires -Version 7.0
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build')
)
$ErrorActionPreference = 'Stop'
$buildDir = (Resolve-Path $BuildDir).Path
$sourceDll = Join-Path $buildDir 'eime_tip.dll'

Write-Host "[E-IME] Compiling eime_tsf..." -ForegroundColor Cyan
cmake --build $buildDir --target eime_tsf --config Release
if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $sourceDll)) {
    $sourceDll = Join-Path $buildDir 'Release\eime_tip.dll'
}

Write-Host "[E-IME] Deploying immutable version..." -ForegroundColor Cyan
& (Join-Path $PSScriptRoot 'install.ps1') -SourceDll $sourceDll -UseExistingRegistration
