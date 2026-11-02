#Requires -Version 5.1
param(
    [string]$SourceDll,
    [switch]$UseExistingRegistration
)
$ErrorActionPreference = 'Stop'
$clsid = '{D8E42B10-7A5C-4E91-9B2A-1F3C5D7E9A01}'
$profile = "Software\Microsoft\CTF\TIP\$clsid\LanguageProfile\0x00000404\{D8E42B11-7A5C-4E91-9B2A-1F3C5D7E9A01}"
if (-not $SourceDll) {
    $candidates = @(
        (Join-Path $PSScriptRoot '..\eime_tip.dll'),
        (Join-Path $PSScriptRoot '..\build\eime_tip.dll'),
        (Join-Path $PSScriptRoot '..\build\Release\eime_tip.dll'),
        (Join-Path $PSScriptRoot '..\build_out\eime_tip.dll')
    )
    $registeredKey = "HKCU:\Software\Classes\CLSID\$clsid\InprocServer32"
    if (Test-Path -LiteralPath $registeredKey) {
        $candidates += (Get-Item -LiteralPath $registeredKey).GetValue('')
    }
    $SourceDll = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1
    if (-not $SourceDll) { throw 'No E-IME DLL found. Build the project first, or pass -SourceDll <path>.' }
}
$SourceDll = (Resolve-Path -LiteralPath $SourceDll).Path
$hash = (Get-FileHash -LiteralPath $SourceDll -Algorithm SHA256).Hash.ToLowerInvariant()
$dataRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\data')).Path
$sourceDictionary = Join-Path $dataRoot 'core_dict.txt'
if ((Get-Item -LiteralPath $sourceDictionary).Length -eq 0) { throw 'Core dictionary is empty.' }
$dictionaryHash = (Get-FileHash -LiteralPath $sourceDictionary -Algorithm SHA256).Hash.ToLowerInvariant()
# Keep the DLL and its core dictionary local, including before cloud drives mount.
# Include dictionary identity so data updates also create a new immutable bundle.
$bundle = $hash + '-' + $dictionaryHash.Substring(0, 16)
$versionDir = Join-Path $env:LOCALAPPDATA "E-IME\versions\$bundle"
$installedDll = Join-Path $versionDir 'eime_tip.dll'
New-Item -ItemType Directory -Path $versionDir -Force | Out-Null
if (Test-Path -LiteralPath $installedDll) {
    if ((Get-FileHash -LiteralPath $installedDll).Hash.ToLowerInvariant() -ne $hash) {
        throw 'Installed version hash mismatch; refusing to overwrite it.'
    }
} else {
    Copy-Item -LiteralPath $SourceDll -Destination $installedDll
}
if ((Get-FileHash -LiteralPath $installedDll -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash) {
    throw 'Copied DLL hash mismatch; refusing to register this bundle.'
}
$installedData = Join-Path $versionDir 'data'
New-Item -ItemType Directory -Path $installedData -Force | Out-Null
$installedDictionary = Join-Path $installedData 'core_dict.txt'
if (-not (Test-Path -LiteralPath $installedDictionary)) {
    Copy-Item -LiteralPath $sourceDictionary -Destination $installedDictionary
}
if ((Get-FileHash -LiteralPath $installedDictionary -Algorithm SHA256).Hash.ToLowerInvariant() -ne $dictionaryHash) {
    throw 'Installed dictionary hash mismatch; refusing to register this bundle.'
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

$tip = "0404:$clsid{D8E42B11-7A5C-4E91-9B2A-1F3C5D7E9A01}"
# Get-/Set-WinUserLanguageList are CDXML wrappers that only marshal under Windows
# PowerShell 5.1; pwsh 7 throws "Cannot marshal 'parameter #2'". Delegate to 5.1.
$langList = @'
$tip = "TIP_PLACEHOLDER"
$list = Get-WinUserLanguageList
$changed = $false
foreach ($z in @($list | Where-Object { $_.LanguageTag -like "zh-Hant*" })) {
    if (-not ($z.InputMethodTips -contains $tip)) {
        $z.InputMethodTips.Add($tip)
        $changed = $true
        Write-Output "[E-IME] Added to WinUserLanguageList ($($z.LanguageTag))"
    }
}
if ($changed) { Set-WinUserLanguageList $list -Force }
'@
$langList = $langList.Replace('TIP_PLACEHOLDER', $tip)
$langFile = Join-Path $env:TEMP ("eime-langlist-" + $PID + ".ps1")
[IO.File]::WriteAllText($langFile, $langList, [Text.UTF8Encoding]::new($true))
$ps51 = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
try {
    & $ps51 -NoProfile -ExecutionPolicy Bypass -File $langFile
} catch {
    Write-Warning "Could not update WinUserLanguageList: $_"
} finally {
    Remove-Item -LiteralPath $langFile -Force -ErrorAction SilentlyContinue
}
Write-Host "[E-IME] Installed immutable version: $installedDll"
Write-Host "[E-IME] SHA256: $hash"
Write-Host "[E-IME] Local core dictionary: $installedDictionary"
