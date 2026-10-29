#requires -version 7.0
param(
    [Parameter(Mandatory)][string]$MtPath,
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$OutputPath
)
$ErrorActionPreference = 'Stop'
& $MtPath "-inputresource:$Executable;#1" "-out:$OutputPath"
if ($LASTEXITCODE -ne 0) { throw 'Could not extract executable manifest.' }
[xml]$manifest = Get-Content -LiteralPath $OutputPath -Raw -Encoding utf8
$level = $manifest.SelectSingleNode('//*[local-name()="requestedExecutionLevel"]')
if ($null -eq $level -or $level.level -ne 'requireAdministrator' -or $level.uiAccess -ne 'false') {
    throw 'The executable must request requireAdministrator with uiAccess=false.'
}
Write-Output 'PASS: embedded executable manifest requires administrator privileges.'
