#Requires -Version 7.0
# E-IME (Enhanced IME) Windows TSF 一鍵反註冊與移除腳本

$ErrorActionPreference = "Stop"
$installedKey = 'HKCU:\Software\Classes\CLSID\{D8E42B10-7A5C-4E91-9B2A-1F3C5D7E9A01}\InprocServer32'
$DllPath = if (Test-Path $installedKey) { (Get-Item $installedKey).GetValue('') } else { Join-Path $PSScriptRoot '..\build\eime_tip.dll' }
if (-not (Test-Path $DllPath)) {
    $DllPath = Join-Path $PSScriptRoot "..\build\eime_tsf.dll"
}
$ResolvedDll = Resolve-Path $DllPath -ErrorAction SilentlyContinue

if (-not $ResolvedDll) {
    throw "找不到 build\eime_tip.dll 或 build\eime_tsf.dll"
}

Write-Host "[E-IME] 正在從 Windows 系統反註冊輸入法元件: $($ResolvedDll.Path)" -ForegroundColor Yellow
$startOptions = @{
    FilePath = Join-Path $env:SystemRoot 'System32\regsvr32.exe'
    ArgumentList = "/s /u `"$($ResolvedDll.Path)`""
    WindowStyle = 'Hidden'
    Wait = $true
    PassThru = $true
}
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $startOptions.Verb = 'RunAs'
}
$registration = Start-Process @startOptions
if ($registration.ExitCode -ne 0) {
    throw "E-IME 反註冊失敗，regsvr32 結束碼: $($registration.ExitCode)。請確認已允許管理員權限。"
}
Remove-Item -LiteralPath 'HKCU:\Software\Classes\CLSID\{D8E42B10-7A5C-4E91-9B2A-1F3C5D7E9A01}' -Recurse -ErrorAction SilentlyContinue
Remove-Item -LiteralPath 'HKCU:\Software\Microsoft\CTF\TIP\{D8E42B10-7A5C-4E91-9B2A-1F3C5D7E9A01}' -Recurse -ErrorAction SilentlyContinue
Write-Host "[E-IME] 已成功從 Windows 移除註冊。" -ForegroundColor Green
