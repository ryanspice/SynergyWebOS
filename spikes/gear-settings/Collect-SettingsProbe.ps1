#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_][A-Za-z0-9_.@:\[\]-]{0,253}$')][string]$SshTarget,
    [ValidateRange(1,65535)][int]$Port = 22,
    [Parameter(Mandatory)][ValidateSet('baseline','gear','all-settings')][string]$Phase,
    [Parameter(Mandatory)][string]$OutFile
)
$ErrorActionPreference = 'Stop'
Get-Command ssh -ErrorAction Stop | Out-Null
Get-Command python -ErrorAction Stop | Out-Null
& python (Join-Path $PSScriptRoot 'collect.py') --host $SshTarget --port $Port --phase $Phase --out $OutFile
if ($LASTEXITCODE -ne 0) { throw "Read-only gear-settings probe failed (exit $LASTEXITCODE). No settings were changed." }
