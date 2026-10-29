#Requires -Version 7.0
param([Parameter(Mandatory)][string]$Path)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $Path).Path
if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw 'Expected a runtime staging directory.' }
$expected = @(
    'eime_tip.dll', 'README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md',
    '安裝或重新註冊 E-IME.cmd', 'data/core_dict.txt',
    'scripts/install.ps1', 'scripts/disable.ps1', 'scripts/uninstall.ps1',
    'LICENSES/Apache-2.0.txt', 'LICENSES/GPL-2.0.txt',
    'LICENSES/LGPL-2.1.txt', 'LICENSES/OpenCC-AUTHORS.txt'
)
$files = @(Get-ChildItem -LiteralPath $root -Recurse -File | ForEach-Object {
    [IO.Path]::GetRelativePath($root, $_.FullName).Replace('\', '/')
})
$difference = @(Compare-Object -ReferenceObject $expected -DifferenceObject $files)
if ($difference.Count) { throw "Runtime contents differ from the allowlist:`n$($difference | Out-String)" }
Write-Output "PASS: runtime contains exactly $($expected.Count) allowed files; no extra files."
