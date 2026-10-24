param(
    [string]$SourceDll = (Join-Path $PSScriptRoot '..\build\eime_tip.dll'),
    [switch]$UseExistingRegistration
)
$ErrorActionPreference = 'Stop'
$clsid = '{D8E42B10-7A5C-4E91-9B2A-1F3C5D7E9A01}'
$profile = "Software\Microsoft\CTF\TIP\$clsid\LanguageProfile\0x00000404\{D8E42B11-7A5C-4E91-9B2A-1F3C5D7E9A01}"
$SourceDll = (Resolve-Path -LiteralPath $SourceDll).Path
$hash = (Get-FileHash -LiteralPath $SourceDll -Algorithm SHA256).Hash.ToLowerInvariant()
# Never register a mutable build output or overwrite a DLL already mapped in hosts.
$versionDir = Join-Path $env:LOCALAPPDATA "E-IME\versions\$hash"
$installedDll = Join-Path $versionDir 'eime_tip.dll'
$dataRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\data')).Path
New-Item -ItemType Directory -Path $versionDir -Force | Out-Null
if (Test-Path -LiteralPath $installedDll) {
    if ((Get-FileHash -LiteralPath $installedDll).Hash.ToLowerInvariant() -ne $hash) {
        throw 'Installed version hash mismatch; refusing to overwrite it.'
    }
} else {
    Copy-Item -LiteralPath $SourceDll -Destination $installedDll
}
$rootFile = Join-Path $versionDir 'data-root.txt'
if (Test-Path -LiteralPath $rootFile) {
    if ((Get-Content -LiteralPath $rootFile -Raw).Trim() -ne $dataRoot) {
        throw 'This binary version already belongs to a different data directory.'
    }
} else {
    [IO.File]::WriteAllText($rootFile, $dataRoot, [Text.UTF8Encoding]::new($false))
}
if ($UseExistingRegistration) {
    if (-not (Test-Path "Registry::HKEY_LOCAL_MACHINE\$profile")) {
        throw 'No existing machine TSF profile. Run without -UseExistingRegistration to register it.'
    }
} else {
    $options = @{
        FilePath = Join-Path $env:SystemRoot 'System32\regsvr32.exe'
        ArgumentList = "/s `"$installedDll`""
        WindowStyle = 'Hidden'; Wait = $true; PassThru = $true
    }
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { $options.Verb = 'RunAs' }
    $registration = Start-Process @options
    if ($registration.ExitCode -ne 0) { throw "regsvr32 failed: $($registration.ExitCode)" }
}
# Per-user override also replaces the temporary repair containment, if present.
$com = New-Item -Path "HKCU:\Software\Classes\CLSID\$clsid\InprocServer32" -Force
$com.SetValue('', $installedDll)
$com.SetValue('ThreadingModel', 'Apartment')
$userProfile = New-Item -Path "HKCU:\$profile" -Force
$userProfile.SetValue('Enable', 1, [Microsoft.Win32.RegistryValueKind]::DWord)
Write-Host "[E-IME] Installed immutable version: $installedDll"
Write-Host "[E-IME] SHA256: $hash"
