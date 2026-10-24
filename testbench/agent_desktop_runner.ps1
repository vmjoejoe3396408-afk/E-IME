# E-IME (Enhanced IME) Desktop Agent Automation Test Runner
param(
    [string]$TargetApp = "notepad.exe",
    [int]$TypingDelayMs = 100,
    [string]$ReportPath = "build/legacy_key_injection_report.json"
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$DllPath = (Get-Item 'Registry::HKEY_CLASSES_ROOT\CLSID\{D8E42B10-7A5C-4E91-9B2A-1F3C5D7E9A01}\InprocServer32').GetValue('')

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  E-IME Desktop Agent Test Runner                          " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

if (-not (Test-Path $DllPath)) {
    Write-Error "DLL not found: $DllPath"
    exit 1
}

# Registration is handled by scripts/install.ps1. Do not replace a pinned deployment.
Write-Host "[Agent] Using installed DLL: $DllPath" -ForegroundColor Yellow

# 2. Launch Notepad
Write-Host "[Agent] Launching $TargetApp..." -ForegroundColor Yellow
$proc = Start-Process -FilePath $TargetApp -PassThru
Start-Sleep -Seconds 1

$wshell = New-Object -ComObject WScript.Shell
$wshell.AppActivate($proc.Id)
Start-Sleep -Milliseconds 500

$testResults = @()

$testCases = @(
    @{ Name = "Greeting"; Keys = "su3cl3{ENTER}"; Expected = "NiHao" },
    @{ Name = "TechTerm"; Keys = "w96ru 2u04{ENTER}"; Expected = "TSMC" },
    @{ Name = "TradSimpToggle"; Keys = "^+Fw96ru 2u04{ENTER}^+F"; Expected = "TSMC_Simp" },
    @{ Name = "ShiftToggle"; Keys = "hello world{ENTER}"; Expected = "hello world" },
    @{ Name = "LongSentence"; Keys = "rup wu0 wu0 fu4cp3cl3{ENTER}"; Expected = "GoodWeatherToday" },
    @{ Name = "MidSentenceEdit"; Keys = "rup wu0 ej/ g4cp3cl3{LEFT}{LEFT}{LEFT}{LEFT}{DOWN}2{ENTER}"; Expected = "GoodBusinessToday" },
    @{ Name = "PolyphoneXieNavigation"; Keys = "vu, {DOWN}{RIGHT}{LEFT}{ENTER}"; Expected = "Xie" },
    @{ Name = "TypoProtection"; Keys = "g6k2{BS}2k7{ENTER}"; Expected = "ShiDe" },
    @{ Name = "MidSentenceComma"; Keys = "su3cl3{LEFT}^,su3{ENTER}"; Expected = "NiCommaNiHao" }
)

foreach ($tc in $testCases) {
    Write-Host "[Agent] Running test: $($tc.Name) (Keys: $($tc.Keys))... " -NoNewline
    $wshell.AppActivate($proc.Id)
    Start-Sleep -Milliseconds 200
    $wshell.SendKeys($tc.Keys)
    Start-Sleep -Milliseconds 300
    Write-Host "[SENT]" -ForegroundColor Green
    $testResults += @{
        TestName = $tc.Name
        Keys = $tc.Keys
        Timestamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
        Status = "COMPLETED"
    }
}

# Export report
$reportDir = Split-Path $ReportPath -Parent
if ($reportDir -and -not (Test-Path $reportDir)) {
    New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
}

$report = @{
    Agent = "Computer-Use Desktop Agent"
    Timestamp = (Get-Date).ToString("o")
    TargetApp = $TargetApp
    TotalTests = $testResults.Count
    Results = $testResults
}

$report | ConvertTo-Json -Depth 4 | Set-Content -Path $ReportPath -Encoding UTF8
Write-Host "`n[Agent] Test finished! Report saved to: $ReportPath" -ForegroundColor Green

# Close notepad
$proc.CloseMainWindow() | Out-Null
Start-Sleep -Milliseconds 500
if (-not $proc.HasExited) {
    $wshell.SendKeys("{TAB}{ENTER}")
    Start-Sleep -Milliseconds 300
    if (-not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force
    }
}
