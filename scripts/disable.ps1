#Requires -Version 7.0
# Block future loads for this user; hosts that already loaded a DLL retain it until exit.
$ErrorActionPreference = 'Stop'
$clsid = '{D8E42B10-7A5C-4E91-9B2A-1F3C5D7E9A01}'
$key = New-Item -Path "HKCU:\Software\Classes\CLSID\$clsid\InprocServer32" -Force
$key.SetValue('', (Join-Path $env:LOCALAPPDATA 'E-IME\DISABLED.dll'))
$profile = New-Item -Path "HKCU:\Software\Microsoft\CTF\TIP\$clsid\LanguageProfile\0x00000404\{D8E42B11-7A5C-4E91-9B2A-1F3C5D7E9A01}" -Force
$profile.SetValue('Enable', 0, [Microsoft.Win32.RegistryValueKind]::DWord)
Write-Host '[E-IME] New host loads disabled for this user. Run install.ps1 to re-enable.'
