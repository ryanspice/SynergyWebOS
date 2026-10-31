# Minimal assertion helpers for tests/lib/Run-Tests.ps1 (no Pester dependency).
# Functions here are resolved as dynamic script functions and can be used directly
# from test scriptblocks (PowerShell resolves them at call time in the parent scope).
Set-StrictMode -Version 2.0

function Assert-True {
    param([Parameter(Mandatory)][AllowNull()][AllowEmptyCollection()]$Condition, [string]$Message = 'expected true')
    if (-not $Condition) { throw "Assert-True failed: $Message" }
}
function Assert-False {
    param([Parameter(Mandatory)][AllowNull()][AllowEmptyCollection()]$Condition, [string]$Message = 'expected false')
    if ($Condition) { throw "Assert-False failed: $Message" }
}
function Assert-Equal {
    param([AllowNull()][AllowEmptyString()]$Expected, [AllowNull()][AllowEmptyString()]$Actual, [string]$Message = '')
    if ("$Expected" -cne "$Actual") { throw "Assert-Equal failed: expected '$Expected', got '$Actual'. $Message" }
}
function Assert-Match {
    param([AllowEmptyString()][string]$Text, [Parameter(Mandatory)][string]$Pattern, [string]$Message = '')
    if ($Text -notmatch $Pattern) { throw "Assert-Match failed: pattern '$Pattern' not found. $Message" }
}
function Assert-NotMatch {
    param([AllowEmptyString()][string]$Text, [Parameter(Mandatory)][string]$Pattern, [string]$Message = '')
    if ($Text -match $Pattern) { throw "Assert-NotMatch failed: forbidden pattern '$Pattern'. $Message" }
}
function Assert-Path {
    param([Parameter(Mandatory)][string]$Path, [string]$Message = '')
    if (-not (Test-Path -LiteralPath $Path)) { throw "Assert-Path failed: missing '$Path'. $Message" }
}
function Assert-NotPath {
    param([Parameter(Mandatory)][string]$Path, [string]$Message = '')
    if (Test-Path -LiteralPath $Path) { throw "Assert-NotPath failed: exists '$Path'. $Message" }
}
function Get-ShPath {
    <# Git for Windows ships a POSIX sh/bash; fall back to WSL bash if that is all there is. #>
    foreach ($c in 'sh', 'bash') {
        $p = Get-Command $c -ErrorAction SilentlyContinue
        if ($p -and $p.Source -notmatch '(?i)\\Windows\\system32\\bash\.exe$') { return $p.Source }
    }
    $gitSh = @('C:\Program Files\Git\bin\sh.exe', 'C:\Program Files\Git\usr\bin\sh.exe') | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if ($gitSh) { return $gitSh }
    return $null
}
function Get-WslPath {
    <# Converts a Windows path to the /mnt/<drive>/... form used by WSL. #>
    param([Parameter(Mandatory)][string]$Path)
    $full = [System.IO.Path]::GetFullPath($Path)
    $drive = $full.Substring(0, 1).ToLowerInvariant()
    return '/mnt/' + $drive + ($full.Substring(2) -replace '\\', '/')
}
function Test-ExitWithCode {
    <# Runs a command and returns its real exit code (Pester-safe, no $LASTEXITCODE leakage). #>
    param([Parameter(Mandatory)][string]$FilePath, [string[]]$Arguments = @(), [string]$WorkingDirectory)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FilePath
    $psi.Arguments = ($Arguments | ForEach-Object { if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }) -join ' '
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    if ($WorkingDirectory) { $psi.WorkingDirectory = $WorkingDirectory }
    $p = [System.Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEnd()
    $e = $p.StandardError.ReadToEnd()
    $p.WaitForExit()
    return [pscustomobject]@{ ExitCode = $p.ExitCode; Output = ($o + $e) }
}
function Skip-Test { param([string]$Reason) throw ("SKIP: " + $Reason) }
function Block-Test { param([string]$Reason) throw ("BLOCKED: " + $Reason) }

function Assert-NoIssues {
    <# Fails with a readable list when a check helper returns any error issues. #>
    param([AllowNull()][object[]]$Issues, [string]$Context = '')
    $errs = @($Issues | Where-Object { $_ -and $_.severity -eq 'error' })
    if ($errs.Count -gt 0) {
        $lines = $errs | ForEach-Object { "  [$($_.id)] $($_.path): $($_.message)" }
        throw ("$Context produced $($errs.Count) error(s):`n" + ($lines -join "`n"))
    }
    return "ok"
}

function Assert-OnlyWarningsAbove {
    <# Fails on any error; returns warnings text for the detail column. #>
    param([AllowNull()][object[]]$Issues)
    Assert-NoIssues -Issues $Issues | Out-Null
    $w = @($Issues | Where-Object { $_ -and $_.severity -eq 'warn' })
    if ($w.Count -eq 0) { return 'ok' }
    return ('ok (warnings: ' + (($w | ForEach-Object { $_.id }) -join ', ') + ')')
}

function New-FixtureApp {
    <# Writes a minimal valid webOS app fixture into $Path; returns the app dir. #>
    param([Parameter(Mandatory)][string]$Path, [hashtable]$AppinfoOverride = @{}, [switch]$SkipIcon)
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
    $appinfo = [ordered]@{
        id           = 'dev.hurrspice.fixture'
        version      = '0.0.1'
        vendor       = 'Fixture'
        type         = 'web'
        main         = 'index.html'
        title        = 'Fixture'
        icon         = 'icon.png'
        largeIcon    = 'icon-large.png'
        resolution   = '1920x1080'
        requiredACG  = @()
    }
    foreach ($k in $AppinfoOverride.Keys) { $appinfo[$k] = $AppinfoOverride[$k] }
    $json = $appinfo | ConvertTo-Json -Depth 5
    [System.IO.File]::WriteAllText((Join-Path $Path 'appinfo.json'), $json, [System.Text.UTF8Encoding]::new($false))
    $html = @'
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>Fixture</title>
  <link rel="stylesheet" href="styles.css" />
</head>
<body>
  <main><h1>Fixture</h1><p id="status">idle</p></main>
  <script src="app.js"></script>
</body>
</html>
'@
    [System.IO.File]::WriteAllText((Join-Path $Path 'index.html'), $html, [System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::WriteAllText((Join-Path $Path 'styles.css'), "body { font-size: 20px; }`n", [System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::WriteAllText((Join-Path $Path 'app.js'), "(() => { 'use strict'; document.getElementById('status').textContent = 'ok'; })();`n", [System.Text.UTF8Encoding]::new($false))
    if (-not $SkipIcon) {
        $png = [Convert]::FromBase64String('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==')
        [System.IO.File]::WriteAllBytes((Join-Path $Path 'icon.png'), $png)
        [System.IO.File]::WriteAllBytes((Join-Path $Path 'icon-large.png'), $png)
    }
    return $Path
}
