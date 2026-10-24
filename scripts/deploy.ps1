#Requires -Version 7.0
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build')
)
$ErrorActionPreference = 'Stop'
$buildDir = (Resolve-Path $BuildDir).Path
$sourceDll = Join-Path $buildDir 'eime_tip.dll'

Write-Host "[E-IME] Compiling eime_tsf..." -ForegroundColor Cyan
ninja -C $buildDir eime_tsf

Write-Host "[E-IME] Deploying immutable version..." -ForegroundColor Cyan
& (Join-Path $PSScriptRoot 'install.ps1') -SourceDll $sourceDll -UseExistingRegistration
