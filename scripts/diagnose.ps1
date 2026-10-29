#Requires -Version 7.0
param(
    [ValidateSet('Verify', 'Record', 'Report')][string]$Action = 'Verify',
    [string]$BuildDirectory = 'build/mingw-diagnostic',
    [string]$OutputDirectory,
    [ValidateRange(1, 60)][int]$Seconds = 20,
    [switch]$Prepared
)
$ErrorActionPreference = 'Stop'
$provider = '{5C0C84B5-74C2-4B35-94AA-B0158721A912}'
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $env:LOCALAPPDATA ('E-IME/diagnostics/' + [guid]::NewGuid().ToString('N'))
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$etl = Join-Path $OutputDirectory 'trace.etl'
if ($Action -ne 'Report' -and (Test-Path -LiteralPath $etl)) {
    throw 'Use a new output directory; an existing trace will not be overwritten.'
}

function New-Report {
    if (-not (Test-Path -LiteralPath $etl)) { throw "Missing $etl" }
    $xmlPath = Join-Path $OutputDirectory 'events.xml'
    $summaryPath = Join-Path $OutputDirectory 'etw-summary.txt'
    & tracerpt.exe $etl -of XML -o $xmlPath -summary $summaryPath -y | Out-Null
    if ($LASTEXITCODE) { throw "tracerpt failed: $LASTEXITCODE" }
    [xml]$xml = Get-Content -LiteralPath $xmlPath -Raw
    $rows = @(foreach ($event in $xml.SelectNodes("//*[local-name()='Event']")) {
        if ($event.System.Provider.Guid -ne $provider) { continue }
        $fields = [ordered]@{ Process = [string]$event.System.Execution.ProcessID }
        foreach ($data in $event.SelectNodes(".//*[local-name()='Data'][@Name]")) {
            $fields[$data.GetAttribute('Name')] = $data.InnerText
        }
        if ($fields['Kind']) {
            if ($fields['Schema'] -ne '1') { throw 'Unsupported E-IME event schema.' }
            [pscustomobject]$fields
        }
    })
    if (-not $rows.Count) { throw 'No E-IME events decoded. Confirm the diagnostic DLL was loaded and tracing was enabled.' }
    $rows | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'events.json') -Encoding utf8
    $failures = @($rows | Where-Object { $_.PSObject.Properties['Result'] -and [uint64]$_.Result -ge 2147483648 })
    $issues = [Collections.Generic.List[string]]::new()
    $loss = [regex]::Match((Get-Content -LiteralPath $summaryPath -Raw), 'Total\s+Events\s+Lost\s+(\d+)')
    $eventsLost = if ($loss.Success) { [uint64]$loss.Groups[1].Value } else { $null }
    if ($null -eq $eventsLost -or $eventsLost -ne 0) { $issues.Add('ETW event loss is nonzero or could not be determined') }
    foreach ($group in ($rows | Group-Object Process, Service)) {
        $ordered = @($group.Group | Sort-Object { [uint64]$_.Sequence })
        if ($ordered[0].Kind -ne 'Module') { $issues.Add("Service start/module metadata not captured for $($group.Name)") }
        for ($i = 1; $i -lt $ordered.Count; ++$i) {
            if ([uint64]$ordered[$i].Sequence -ne [uint64]$ordered[$i-1].Sequence + 1) {
                $issues.Add("Event gap or duplicate in service $($group.Name)")
            }
        }
        $begin = @($ordered | Where-Object Kind -eq EditBegin)
        $end = @($ordered | Where-Object Kind -eq EditEnd)
        if ($begin.Count -ne $end.Count) { $issues.Add("Incomplete edit span in service $($group.Name)") }
        for ($i = 0; $i -lt [Math]::Min($begin.Count, $end.Count); ++$i) {
            if ($begin[$i].Edit -ne $end[$i].Edit -or [uint64]$begin[$i].Sequence -ge [uint64]$end[$i].Sequence) {
                $issues.Add("Mismatched edit span in service $($group.Name)")
            }
        }
        $previous = 0L
        foreach ($entry in $begin) {
            if ([uint64]$entry.Edit -le $previous) { $issues.Add("Out-of-order edit in service $($group.Name)") }
            $previous = [uint64]$entry.Edit
        }
        $pending = [Collections.Generic.Queue[uint64]]::new()
        foreach ($entry in $ordered) {
            switch ($entry.Kind) {
                Enqueue { $pending.Enqueue([uint64]$entry.Edit) }
                EditBegin {
                    if (-not $pending.Count -or $pending.Peek() -ne [uint64]$entry.Edit) {
                        $issues.Add("Edit did not consume the oldest queued snapshot in service $($group.Name)")
                    } else { [void]$pending.Dequeue() }
                }
                Cancel {
                    if ($pending.Count -ne [uint64]$entry.Value) { $issues.Add("Cancellation count mismatch in service $($group.Name)") }
                    $pending.Clear()
                }
            }
        }
        if ($pending.Count) { $issues.Add("Unfinished queued edits in service $($group.Name); capture may be incomplete") }
    }
    $modules = @($rows | Where-Object Kind -eq Module)
    $binaries = @(foreach ($path in ($modules.ModulePath | Sort-Object -Unique)) {
        try {
            Get-FileHash -LiteralPath $path -Algorithm SHA256 | Select-Object Path, Hash
        } catch { [pscustomobject]@{ Path = $path; Hash = 'unavailable' } }
    })
    $report = [ordered]@{
        Schema = 1; Events = $rows.Count; EventsLost = $eventsLost
        OS = [Environment]::OSVersion.VersionString
        Modules = $modules; Binaries = $binaries
        Counts = @($rows | Group-Object Kind | Select-Object Name, Count)
        HostFailures = $failures
        IntegrityIssues = @($issues)
        Trace = $etl
    }
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'report.json') -Encoding utf8
    return [pscustomobject]$report
}

if ($Action -eq 'Report') { New-Report | ConvertTo-Json -Depth 8; return }

if ($Action -eq 'Verify' -and -not $Prepared) {
    $build = (Resolve-Path -LiteralPath $BuildDirectory).Path
    $exe = @((Join-Path $build 'eime_trace_scenario.exe'), (Join-Path $build 'RelWithDebInfo/eime_trace_scenario.exe')) |
        Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    $dll = @((Join-Path $build 'eime_tip.dll'), (Join-Path $build 'RelWithDebInfo/eime_tip.dll')) |
        Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $exe -or -not $dll) { throw 'Build the diagnostic preset first (scenario executable and DLL required).' }
    $scenario = Join-Path $OutputDirectory 'scenario'
    New-Item -ItemType Directory -Path "$scenario/build", "$scenario/data" -Force | Out-Null
    Copy-Item -LiteralPath $exe -Destination "$scenario/eime_trace_scenario.exe"
    Copy-Item -LiteralPath $dll -Destination "$scenario/build/eime_tip.dll"
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../data/core_dict.txt') -Destination "$scenario/data/core_dict.txt"
    Get-FileHash -LiteralPath $dll -Algorithm SHA256 | Select-Object Algorithm, Hash, Path |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'binary.json') -Encoding utf8
}

$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    if ($Prepared) { throw 'ETW recording requires an elevated process.' }
    # Copy the self-contained controller locally so elevation does not depend on a mapped drive.
    $controller = Join-Path $OutputDirectory 'controller.ps1'
    Copy-Item -LiteralPath $PSCommandPath -Destination $controller
    Write-Host "Starting elevated ETW $Action. Results: $OutputDirectory"
    $arguments = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -Action {1} -OutputDirectory "{2}" -Seconds {3} -Prepared' -f $controller, $Action, $OutputDirectory, $Seconds
    $child = Start-Process -FilePath (Get-Command pwsh).Source -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -Wait -PassThru
    if ($child.ExitCode) { throw "Elevated recording failed ($($child.ExitCode)); inspect $OutputDirectory" }
    Get-Content -LiteralPath (Join-Path $OutputDirectory 'report.json') -Raw
    return
}

$session = 'EIME-' + [guid]::NewGuid().ToString('N')
$started = $false
try {
    & logman.exe create trace $session -p $provider 0x1 4 -o $etl -f bincirc -max 64 -bs 64 -nb 8 64 -ets |
        Set-Content -LiteralPath (Join-Path $OutputDirectory 'capture-start.txt')
    if ($LASTEXITCODE) { throw "logman start failed: $LASTEXITCODE" }
    $started = $true
    if ($Action -eq 'Verify') {
        $scenario = Join-Path $OutputDirectory 'scenario'
        Push-Location $scenario
        try {
            & ./eime_trace_scenario.exe (Join-Path $scenario 'build/eime_tip.dll') |
                Set-Content -LiteralPath (Join-Path $OutputDirectory 'scenario.txt')
            if ($LASTEXITCODE) { throw "Diagnostic scenario failed: $LASTEXITCODE" }
        } finally { Pop-Location }
    } else {
        Write-Host "Recording for $Seconds seconds. Reproduce the issue in a newly opened diagnostic host."
        Start-Sleep -Seconds $Seconds
    }
} catch {
    $_ | Out-String | Set-Content -LiteralPath (Join-Path $OutputDirectory 'failure.txt')
    throw
} finally {
    if ($started) {
        & logman.exe stop $session -ets | Set-Content -LiteralPath (Join-Path $OutputDirectory 'capture-stop.txt')
        if ($LASTEXITCODE) { Write-Warning "Could not stop ETW session $session; stop it with logman stop $session -ets" }
    }
}
$report = New-Report
if ($Action -eq 'Verify') {
    foreach ($kind in 'Module', 'Key', 'Enqueue', 'EditBegin', 'EditEnd', 'HostCall') {
        if (-not ($report.Counts | Where-Object Name -eq $kind)) { throw "Missing event kind: $kind" }
    }
    $expected = @($report.HostFailures | Where-Object { $_.Operation -eq 'Preview.SetText' -and [uint64]$_.Result -eq 2147942405 })
    if ($expected.Count -ne 1 -or $report.HostFailures.Count -ne 1 -or $report.IntegrityIssues.Count) {
        throw "Trace verification failed; inspect $OutputDirectory/report.json"
    }
    $binary = Get-Content -LiteralPath (Join-Path $OutputDirectory 'binary.json') -Raw | ConvertFrom-Json
    if (-not ($report.Binaries | Where-Object Hash -eq $binary.Hash)) { throw 'Loaded DLL hash does not match the scenario binary.' }
    Write-Host 'PASS: ETW decoded mixed-input ordering and exactly one injected Preview.SetText / E_ACCESSDENIED failure.'
}
$report | ConvertTo-Json -Depth 8
